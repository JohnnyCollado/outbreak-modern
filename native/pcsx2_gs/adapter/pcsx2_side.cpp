// pcsx2_side: the PCSX2 half of the runner's external GS (ob_gs_bridge.h). Compiled with ob_pcsx2_gs's
// PUBLIC defines/options; never sees PS2Recomp headers. One mutex serialises every PCSX2 call.
// Threading (PCSX2's MTGS scheme, pcsx2/MTGS.cpp): by default every PCSX2 GS call runs on a GS thread owned here,
// fed by gs_ring.h; the ob_gs_* entry points (game thread) only copy the command into the ring. Readback and snapshot
// wait for the GS thread to execute everything queued before them. PS2X_GS_THREAD=0: every call runs inline on the
// caller's thread, as before. Window attach/resize stay on the main thread in both modes (the child window must
// belong to the thread that pumps the parent's messages); the resize reaches PCSX2 at the next executed vsync.
#include "common/RedtapeWindows.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/FPControl.h"
#include "common/Path.h"
#include "common/WindowInfo.h"

#include "Config.h"
#include "GS/GS.h"
#include "GS/Renderers/Common/GSRenderer.h"

#include "ob_pcsx2_gs.h"
#include "ob_gs_bridge.h"
#include "gs_ring.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(__ANDROID__)
// Android (Stage A1): PCSX2 presents into the NativeActivity's ANativeWindow through VK_KHR_android_surface
// (upstream/pcsx2/0001). raylib (rcore_android.c) owns that window with an EGL surface; attach releases the surface
// (the context stays current without one) so Vulkan can connect to the window.
#include <EGL/egl.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <cerrno>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
extern "C" struct android_app* GetAndroidApp(void);
using HWND = void*;
using DWORD = uint32_t;
static DWORD GetCurrentThreadId() { return static_cast<DWORD>(gettid()); }
#endif

namespace
{
	std::mutex s_mu;
	ObGsRenderer s_renderer = ObGsRenderer::SW;
	bool s_tried = false, s_open = false, s_logged_snap = false;
	alignas(64) u8 s_basemem[0x2000] = {};
	std::vector<u8> s_rgba;
	HWND s_child = nullptr;   // set by ob_gs_attach_window: PCSX2 presents into this child of the runner window
	                          // (Android: the ANativeWindow itself)
	bool s_visible = false;   // parent window visible at attach: Mailbox present, else Disabled
	std::atomic<u64> s_pending_size{0}; // (w << 32) | h from ob_gs_window_resized; 0 = none; applied in ob_gs_vsync

	void StopGsThread(const char* why); // below

#if defined(__ANDROID__)
	bool MakeDirs(const std::string& path)
	{
		for (size_t i = 1; i <= path.size(); ++i)
		{
			if (i == path.size() || path[i] == '/')
			{
				const std::string part = path.substr(0, i);
				if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
					return false;
			}
		}
		return true;
	}

	// The APK's assets/resources tree (PCSX2's shaders) copied to <internal files>/resources once per APK build:
	// PCSX2 reads resources as files (GSDevice.cpp:445). assets/resources/files.txt (written by tools/ob/android.sh)
	// lists them, as AAssetDir lists no subdirectories; a copy whose files.txt matches is kept.
	std::string ExtractResources(const char* internal_dir)
	{
		android_app* app = GetAndroidApp();
		AAssetManager* am = (app && app->activity) ? app->activity->assetManager : nullptr;
		const std::string root = std::string(internal_dir) + "/resources";
		if (!am)
			return root;
		auto read_asset = [am](const std::string& name, std::string& out) {
			AAsset* a = AAssetManager_open(am, name.c_str(), AASSET_MODE_STREAMING);
			if (!a)
				return false;
			out.resize(static_cast<size_t>(AAsset_getLength64(a)));
			const int got = out.empty() ? 0 : AAsset_read(a, out.data(), out.size());
			AAsset_close(a);
			return got == static_cast<int>(out.size());
		};
		std::string list, old;
		if (!read_asset("resources/files.txt", list))
		{
			std::fprintf(stderr, "[pcsx2-gs] android: no assets/resources/files.txt in the APK\n");
			return root;
		}
		if (FILE* f = std::fopen((root + "/files.txt").c_str(), "rb"))
		{
			char buf[4096];
			size_t n;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
				old.append(buf, n);
			std::fclose(f);
		}
		if (old == list)
			return root;
		size_t count = 0, begin = 0;
		while (begin < list.size())
		{
			size_t end = list.find('\n', begin);
			if (end == std::string::npos)
				end = list.size();
			std::string rel = list.substr(begin, end - begin);
			begin = end + 1;
			if (!rel.empty() && rel.back() == '\r')
				rel.pop_back();
			if (rel.empty())
				continue;
			std::string bytes;
			const std::string dst = root + "/" + rel;
			if (!read_asset("resources/" + rel, bytes) || !MakeDirs(dst.substr(0, dst.rfind('/'))))
				continue;
			if (FILE* f = std::fopen(dst.c_str(), "wb"))
			{
				std::fwrite(bytes.data(), 1, bytes.size(), f);
				std::fclose(f);
				++count;
			}
		}
		if (FILE* f = std::fopen((root + "/files.txt").c_str(), "wb"))
		{
			std::fwrite(list.data(), 1, list.size(), f);
			std::fclose(f);
		}
		std::fprintf(stderr, "[pcsx2-gs] android: %zu resource files extracted to %s\n", count, root.c_str());
		return root;
	}

	bool CreateHiddenWindow()
	{
		return false; // Android always attaches the activity's window first
	}

	GSRendererType ToType(ObGsRenderer r) { return r == ObGsRenderer::VK ? GSRendererType::VK : GSRendererType::SW; }
#else
	// Child of the runner window: no background, disabled and hit-test transparent so input stays with the parent.
	LRESULT CALLBACK ChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_NCHITTEST)
			return HTTRANSPARENT;
		// The runner's CloseWindow (main thread, after it joined the game thread) destroys the parent and so this
		// child: the GS thread executes what is queued and ends first, so nothing presents into a dead window.
		if (msg == WM_DESTROY)
			StopGsThread("window destroyed");
		return DefWindowProcW(hwnd, msg, wp, lp);
	}

	GSRendererType ToType(ObGsRenderer r) { return r == ObGsRenderer::VK ? GSRendererType::VK : GSRendererType::SW; }

	// As smoke/main.cpp CreateHiddenWindow: hidden Win32 window handed to Host::AcquireRenderWindow.
	bool CreateHiddenWindow()
	{
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"ObPcsx2GsRunner";
		if (!RegisterClassExW(&wc))
			return false;
		HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"ob_pcsx2_gs", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
			CW_USEDEFAULT, 640, 448, nullptr, nullptr, wc.hInstance, nullptr);
		if (!hwnd)
			return false;
		RECT rc = {};
		GetClientRect(hwnd, &rc);
		WindowInfo wi;
		wi.type = WindowInfo::Type::Win32;
		wi.window_handle = hwnd;
		wi.surface_width = static_cast<u32>(rc.right - rc.left);
		wi.surface_height = static_cast<u32>(rc.bottom - rc.top);
		wi.surface_scale = 1.0f;
		ObPcsx2GS::SetRenderWindow(wi);
		return true;
	}
#endif

	// Same settings as smoke/main.cpp MakeOptions (reference manifest, plan ## Interfaces), except a visible window
	// deinterlaces: Outbreak runs SMODE2 INT=1 FFMD=0 with both circuits merged one line apart (PMODE 0x7f27), and
	// Off shows that as a per-field one-line shake. Snapshots read local memory, so the gate is unaffected.
	Pcsx2Config::GSOptions MakeOptions(GSRendererType r, bool visible)
	{
		Pcsx2Config::GSOptions o;
		o.Renderer = r;
		o.UpscaleMultiplier = 1.0f;
		o.InterlaceMode = visible ? GSInterlaceMode::Automatic : GSInterlaceMode::Off; // was Off for every window
		o.LinearPresent = GSPostBilinearMode::Off;
		o.Dithering = 2;
		o.PCRTCAntiBlur = true;
		o.FXAA = false;
		o.ShadeBoost = false;
		o.PCRTCOffsets = false;
		o.PCRTCOverscan = false;
		o.TVShader = 0;
		o.CASMode = GSCASMode::Disabled;
		o.DisableShaderCache = false; // cache in EmuFolders::Cache: shaders compile once, not on every launch
		return o;
	}

	// Caller holds s_mu. Opens once; a failed open is not retried.
	bool EnsureOpen()
	{
		if (s_tried)
			return s_open;
		s_tried = true;
#if defined(__ANDROID__)
		{
			android_app* app = GetAndroidApp();
			const char* internal = (app && app->activity && app->activity->internalDataPath) ? app->activity->internalDataPath : ".";
			EmuFolders::AppRoot = internal;
			EmuFolders::Resources = ExtractResources(internal);
		}
#else
		EmuFolders::AppRoot = std::string(Path::GetDirectory(FileSystem::GetProgramPath()));
		EmuFolders::Resources = Path::Combine(EmuFolders::AppRoot, "resources");
#endif
		EmuFolders::Cache = Path::Combine(EmuFolders::AppRoot, "cache");
		if (!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), false))
			std::fprintf(stderr, "[pcsx2-gs] shader cache dir %s not created; shaders compile every launch\n", EmuFolders::Cache.c_str());
		const GSRendererType t = ToType(s_renderer);
		// PCSX2's own log (swap chain present mode, fallbacks, resize errors) -> pcsx2-gs.log in the working dir.
		Log::SetFileOutputLevel(LOGLEVEL_DEV, "pcsx2-gs.log");
		const GSVSyncMode vsync = (s_child && s_visible) ? GSVSyncMode::Mailbox : GSVSyncMode::Disabled;
		s_open = (s_child || CreateHiddenWindow()) && GSopen(MakeOptions(t, s_child && s_visible), t, s_basemem, vsync, false);
		std::fprintf(stderr, "[pcsx2-gs] open renderer=%d %s window=%s vsync=%d actual=%d surface=%dx%d\n",
			static_cast<int>(t), s_open ? "ok" : "FAILED", s_child ? "child" : "hidden", static_cast<int>(vsync),
			s_open && g_gs_device ? static_cast<int>(g_gs_device->GetVSyncMode()) : -1,
			s_open && g_gs_device ? g_gs_device->GetWindowWidth() : 0, s_open && g_gs_device ? g_gs_device->GetWindowHeight() : 0);
		std::fflush(stderr);
		return s_open;
	}

	void Put64(u32 off, u64 v) { std::memcpy(s_basemem + off, &v, 8); }
	u64 Get64(u32 off)
	{
		u64 v;
		std::memcpy(&v, s_basemem + off, 8);
		return v;
	}

	// Display size from the mirrored PMODE/DISPFBn/DISPLAYn, as our CPU presentation derives it
	// (gs_cpu_backend.cpp decodeDisplaySize/hasDisplaySetup/PresentFromLocalMemory): per circuit
	// (DW+1)/(MAGH+1) x (DH+1), 640x448 below 64, capped at 640x512; both circuits on -> the larger of each.
	void DisplayRect(u32& w, u32& h)
	{
		const u64 pmode = Get64(0x00);
		u32 cw[2], ch[2];
		bool valid[2];
		for (u32 i = 0; i < 2; i++)
		{
			const u64 fb = Get64(i ? 0x90 : 0x70), d = Get64(i ? 0xA0 : 0x80);
			const u32 dw = (u32)((d >> 32) & 0xFFF), dh = (u32)((d >> 44) & 0x7FF), magh = (u32)((d >> 23) & 0xF);
			valid[i] = ((pmode >> i) & 1) && (((fb >> 9) & 0x3F) != 0 || dw || dh || magh);
			cw[i] = (dw + 1) / (magh + 1);
			ch[i] = dh + 1;
			if (cw[i] < 64 || ch[i] < 64)
				cw[i] = 640, ch[i] = 448;
			cw[i] = std::min<u32>(cw[i], 640);
			ch[i] = std::min<u32>(ch[i], 512);
		}
		if (valid[0] && valid[1])
			w = std::max(cw[0], cw[1]), h = std::max(ch[0], ch[1]);
		else if (valid[0] || valid[1])
			w = cw[valid[0] ? 0 : 1], h = ch[valid[0] ? 0 : 1];
		else
			w = 640, h = 448;
	}
} // namespace

void ob_gs_init(ObGsRenderer renderer)
{
	std::lock_guard<std::mutex> lk(s_mu);
	s_renderer = renderer;
}

// Qwords of the GIFtag units that fit whole in [p, p + qwc*16): GSState::Transfer (GSState.cpp:3392, :3455)
// runs a register handler before checking size when a call ends right after a tag with NLOOP > 0, then size
// underflows and it reads past the buffer. PCSX2's Gif_Unit only hands it whole units; we do the same.
static u32 WholeUnitsQwc(const u8* p, u32 qwc)
{
	u32 at = 0;
	while (at < qwc)
	{
		u64 tag;
		std::memcpy(&tag, p + (size_t)at * 16, 8);
		const u32 nloop = (u32)(tag & 0x7FFF), flg = (u32)((tag >> 58) & 3);
		const u32 nreg = (u32)(tag >> 60) ? (u32)(tag >> 60) : 16;
		const u32 data = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
		if (at + 1 + data > qwc)
			break;
		at += 1 + data;
	}
	return at;
}

// The runtime's VIF1 interpreter resubmits the rest of a PATH2 IMAGE cut at a VIF packet boundary behind a
// synthesized IMAGE GIFtag (ps2_vif1_interpreter.cpp:307-323: NLOOP = the chunk's qwc, EOP on the last chunk,
// every other bit zero). The CPU GS needs that tag; PCSX2 still has the original tag's IMAGE open, so the extra
// qword would shift the stream. True when carry holds an open IMAGE unit and data starts with such a tag.
static bool IsSynthImageContinuation(const std::vector<u8>& carry, const u8* data, u32 n)
{
	if (carry.size() < 16 || n < 32)
		return false;
	u64 open, tag[2];
	std::memcpy(&open, carry.data(), 8);
	std::memcpy(tag, data, 16);
	return ((open >> 58) & 3) == 2 && tag[1] == 0 && (tag[0] & ~0x7FFFull & ~(1ull << 15)) == (2ull << 58) &&
		(tag[0] & 0x7FFF) == n / 16 - 1;
}

// ---- Exec*: the PCSX2 side of each command, on the executing thread (GS thread, or the caller inline) with s_mu held.

static void ExecGif(uint32_t path, const uint8_t* data, uint32_t size_bytes)
{
	static std::vector<u8> s_carry[4];
	if (!EnsureOpen() || size_bytes < 16)
		return;
	std::vector<u8>& c = s_carry[path & 3];
	u32 n = size_bytes & ~15u;
	if (path == 2 && IsSynthImageContinuation(c, data, n))
	{
		data += 16;
		n -= 16;
	}
	if (c.empty())
	{
		const u32 q = WholeUnitsQwc(data, n / 16);
		if (q)
			GSgifTransfer(data, q);
		c.assign(data + (size_t)q * 16, data + n);
	}
	else
	{
		c.insert(c.end(), data, data + n);
		const u32 q = WholeUnitsQwc(c.data(), (u32)(c.size() / 16));
		if (q)
			GSgifTransfer(c.data(), q);
		c.erase(c.begin(), c.begin() + (size_t)q * 16);
	}
	// Only an open IMAGE unit carries into the next packet (its data follows as raw bytes or behind a
	// synthesized tag). Any other cut unit is dropped, as the CPU GS drops it (gs_frontend.cpp processGIFPacket
	// keeps no GIF state across packets): carried, one malformed packet would swallow the path's later packets.
	if (!c.empty())
	{
		u64 open;
		std::memcpy(&open, c.data(), 8);
		if (((open >> 58) & 3) != 2)
			c.clear();
	}
}

static void ExecPriv(uint32_t offset, uint64_t value)
{
	if (EnsureOpen() && offset + 8 <= sizeof(s_basemem))
		Put64(offset, value);
}

static void ExecVsync(const uint64_t priv[15], uint64_t csr, uint64_t imr, uint64_t busdir, uint64_t siglblid)
{
	if (!EnsureOpen())
		return;
	for (u32 i = 0; i < 15; i++)
		Put64(i * 0x10, priv[i]);
	Put64(0x1000, csr);
	Put64(0x1010, imr);
	Put64(0x1040, busdir);
	Put64(0x1080, siglblid);
	static u64 s_last_mode[4] = {~0ull, ~0ull, ~0ull, ~0ull};
	static u32 s_mode_logs = 0;
	const u64 mode[4] = {priv[0], priv[2], priv[8], priv[10]}; // PMODE, SMODE2, DISPLAY1, DISPLAY2
	if (std::memcmp(mode, s_last_mode, sizeof(mode)) != 0 && s_mode_logs < 64)
	{
		s_mode_logs++;
		std::memcpy(s_last_mode, mode, sizeof(mode));
		std::fprintf(stderr, "[pcsx2-gs] display mode pmode=0x%llx smode2 int=%u ffmd=%u display1=0x%llx display2=0x%llx "
			"dispfb1=0x%llx dispfb2=0x%llx\n", (unsigned long long)priv[0], (u32)(priv[2] & 1), (u32)((priv[2] >> 1) & 1),
			(unsigned long long)priv[8], (unsigned long long)priv[10], (unsigned long long)priv[7], (unsigned long long)priv[9]);
		std::fflush(stderr);
	}
	if (const u64 size = s_pending_size.exchange(0))
	{
		GSResizeDisplayWindow(static_cast<u32>(size >> 32), static_cast<u32>(size), 1.0f);
		std::fprintf(stderr, "[pcsx2-gs] resize applied %ux%u surface=%dx%d\n", static_cast<u32>(size >> 32),
			static_cast<u32>(size), g_gs_device ? g_gs_device->GetWindowWidth() : 0, g_gs_device ? g_gs_device->GetWindowHeight() : 0);
		std::fflush(stderr);
	}
	GSvsync((csr & 0x2000) ? 1 : 0, true);
}

#if defined(__ANDROID__)
void ob_gs_attach_window(void*, uint32_t, uint32_t, bool)
{
	std::lock_guard<std::mutex> lk(s_mu);
	if (s_tried || s_child)
		return;
	android_app* app = GetAndroidApp();
	ANativeWindow* win = app ? app->window : nullptr;
	if (!win)
	{
		std::fprintf(stderr, "[pcsx2-gs] android: no ANativeWindow at attach\n");
		return;
	}
	// raylib drew nothing yet; drop its EGL window surface so the window is free for Vulkan. Its context stays
	// current without a surface (EGL_KHR_surfaceless_context), so raylib's per-frame GL calls stay harmless.
	EGLDisplay dpy = eglGetCurrentDisplay();
	EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
	EGLContext ctx = eglGetCurrentContext();
	bool surfaceless = false;
	if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE)
	{
		surfaceless = eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx) == EGL_TRUE;
		if (!surfaceless)
			eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroySurface(dpy, surf);
	}
	s_child = win;
	s_visible = true;
	WindowInfo wi;
	wi.type = WindowInfo::Type::Android;
	wi.window_handle = win;
	wi.surface_width = static_cast<u32>(ANativeWindow_getWidth(win));
	wi.surface_height = static_cast<u32>(ANativeWindow_getHeight(win));
	wi.surface_scale = 1.0f;
	ObPcsx2GS::SetRenderWindow(wi);
	std::fprintf(stderr, "[pcsx2-gs] attach android window %ux%u egl surface released=%d surfaceless=%d\n",
		wi.surface_width, wi.surface_height, surf != EGL_NO_SURFACE ? 1 : 0, surfaceless ? 1 : 0);
	std::fflush(stderr);
}

void ob_gs_window_resized(uint32_t w, uint32_t h)
{
	if (!s_child || w == 0 || h == 0)
		return;
	s_pending_size.store((static_cast<u64>(w) << 32) | h);
}
#else
void ob_gs_attach_window(void* hwnd, uint32_t w, uint32_t h, bool visible)
{
	std::lock_guard<std::mutex> lk(s_mu);
	if (s_tried || s_child || !hwnd)
		return; // the GS is already open (or attach repeated): keep its window
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = ChildWndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = L"ObPcsx2GsChild";
	if (!RegisterClassExW(&wc))
		return;
	s_child = CreateWindowExW(0, wc.lpszClassName, L"ob_pcsx2_gs", WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS,
		0, 0, static_cast<int>(w), static_cast<int>(h), static_cast<HWND>(hwnd), nullptr, wc.hInstance, nullptr);
	if (!s_child)
		return;
	s_visible = visible;
	WindowInfo wi;
	wi.type = WindowInfo::Type::Win32;
	wi.window_handle = s_child;
	wi.surface_width = w;
	wi.surface_height = h;
	wi.surface_scale = 1.0f;
	ObPcsx2GS::SetRenderWindow(wi);
	const LONG_PTR ps = GetWindowLongPtrW(static_cast<HWND>(hwnd), GWL_STYLE);
	std::fprintf(stderr, "[pcsx2-gs] attach child %ux%u visible=%d parent clipchildren=%d clipsiblings=%d\n", w, h,
		visible ? 1 : 0, (ps & WS_CLIPCHILDREN) ? 1 : 0, (ps & WS_CLIPSIBLINGS) ? 1 : 0);
	std::fflush(stderr);
}

void ob_gs_window_resized(uint32_t w, uint32_t h)
{
	if (!s_child || w == 0 || h == 0)
		return;
	SetWindowPos(s_child, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(h), SWP_NOZORDER | SWP_NOACTIVATE);
	s_pending_size.store((static_cast<u64>(w) << 32) | h);
	RECT rc = {};
	GetClientRect(s_child, &rc);
	std::fprintf(stderr, "[pcsx2-gs] resize request %ux%u child=%ldx%ld\n", w, h, rc.right, rc.bottom);
	std::fflush(stderr);
}
#endif

static uint32_t ExecReadLocal(uint8_t* dst, uint32_t max_bytes)
{
	const u32 qwc = max_bytes / 16;
	if (!EnsureOpen() || qwc == 0)
		return 0;
	GSInitAndReadFIFO(dst, qwc);
	return qwc * 16;
}

static void ExecReset()
{
	if (s_open)
		GSreset(false);
}

static const uint8_t* ExecSnapshot(uint32_t* width, uint32_t* height)
{
	if (!EnsureOpen())
		return nullptr;
	u32 dw = 0, dh = 0;
	DisplayRect(dw, dh);
	// 1:1 crop of the displayed rect from PCSX2's local memory, as our CPU presentation reads it
	// (gs_cpu_backend.cpp PresentFromLocalMemory: circuit 1 if enabled else 2, FBP/FBW/PSM, origin DBX/DBY).
	// Was a nearest-neighbour scale of GSSaveSnapshotToMemory's 640x960 texture: shifted/clipped the image.
	// Not GSReadLocalMemoryUnsync: it mutates PCSX2's transfer state (GSState.cpp:3304-3333).
	const u64 pmode = Get64(0x00);
	const bool c1 = (pmode & 1) != 0;
	const u64 fb = Get64(c1 ? 0x70 : 0x90);
	const u32 fbp = (u32)(fb & 0x1FF), fbw = (u32)((fb >> 9) & 0x3F), psm = (u32)((fb >> 15) & 0x1F);
	const u32 dbx = (u32)((fb >> 32) & 0x7FF), dby = (u32)((fb >> 43) & 0x7FF);
	if (!g_gs_renderer || fbw == 0)
		return nullptr;
	GSState& gs = *g_gs_renderer;
	gs.Flush(GSState::GSFlushReason::DOWNLOADFIFO);
	GIFRegBITBLTBUF bb = {};
	bb.SBP = fbp * 32;
	bb.SBW = fbw;
	bb.SPSM = psm;
	gs.InvalidateLocalMem(bb, GSVector4i((int)dbx, (int)dby, (int)(dbx + dw), (int)(dby + dh)));
	if (!s_logged_snap)
	{
		s_logged_snap = true;
		std::fprintf(stderr, "[pcsx2-gs] first snapshot crop %ux%u fbp %u fbw %u psm %u at %u,%u\n", dw, dh, fbp, fbw, psm, dbx, dby);
		std::fflush(stderr);
	}
	s_rgba.resize((size_t)dw * dh * 4);
	const GSLocalMemory& mem = gs.m_mem;
	for (u32 y = 0; y < dh; y++)
	{
		for (u32 x = 0; x < dw; x++)
		{
			const int px = (int)((dbx + x) & 0x7FF), py = (int)((dby + y) & 0x7FF);
			u32 p;
			if (psm == PSMCT16 || psm == PSMCT16S)
			{
				const u32 v = psm == PSMCT16 ? mem.ReadPixel16(px, py, fbp * 32, fbw) : mem.ReadPixel16S(px, py, fbp * 32, fbw);
				p = ((v & 0x1F) << 3) | (((v >> 5) & 0x1F) << 11) | (((v >> 10) & 0x1F) << 19);
			}
			else
				p = mem.ReadPixel32(px, py, fbp * 32, fbw);
			p |= 0xFF000000u;
			std::memcpy(&s_rgba[((size_t)y * dw + x) * 4], &p, 4);
		}
	}
	*width = dw;
	*height = dh;
	return s_rgba.data();
}

// ---- Dispatch: GS thread (default) or inline (PS2X_GS_THREAD=0) ----

namespace
{
	enum Cmd : uint32_t
	{
		kGif = obgs::Ring::kFirstUserCmd, // aux = path; payload = the packet
		kPriv,                            // aux = offset, a = value
		kVsync,                           // payload = VsyncArgs
		kReadLocal,                       // aux = max bytes, a = dst, b = &result (u32)
		kReset,
		kSnapshot,                        // a = &SnapshotResult
	};
	struct VsyncArgs
	{
		u64 priv[15], csr, imr, busdir, siglblid;
	};
	struct SnapshotResult
	{
		const u8* p;
		u32 w, h;
	};

	// PCSX2's MTGS ring is 1 << 19 qwords = 8 MiB (MTGS.h RingBufferSizeFactor); its EE thread stalls at a vsync
	// while more than EmuConfig.GS.VsyncQueueSize (Config.h, default 2 = DEFAULT_FRAME_LATENCY) vsyncs are queued.
	constexpr size_t kRingBytes = size_t(1) << 23;
	constexpr int kQueueAheadVsyncs = 2;

	enum class Mode : int
	{
		Undecided,
		Thread,
		Inline,
	};
	Mode s_mode = Mode::Undecided;      // producer-only (the thread that calls ob_gs_*)
	obgs::Ring* s_ring = nullptr;       // never deleted; StopGsThread ends its thread
	DWORD s_producer_tid = 0;
	u64 s_vsyncs_posted = 0;

	void RingHandler(void*, const obgs::RecordHeader& h, const uint8_t* payload)
	{
		std::lock_guard<std::mutex> lk(s_mu);
		switch (h.cmd)
		{
			case kGif:
				ExecGif(h.aux, payload, h.bytes);
				break;
			case kPriv:
				ExecPriv(h.aux, h.a);
				break;
			case kVsync:
			{
				VsyncArgs v;
				std::memcpy(&v, payload, sizeof(v));
				ExecVsync(v.priv, v.csr, v.imr, v.busdir, v.siglblid);
				break;
			}
			case kReadLocal:
				*reinterpret_cast<u32*>(h.b) = ExecReadLocal(reinterpret_cast<u8*>(h.a), h.aux);
				break;
			case kReset:
				ExecReset();
				break;
			case kSnapshot:
			{
				SnapshotResult* r = reinterpret_cast<SnapshotResult*>(h.a);
				r->p = ExecSnapshot(&r->w, &r->h);
				break;
			}
			default:
				break;
		}
	}

	void RingThreadInit(void*)
	{
#if defined(_WIN32)
		SetThreadDescription(GetCurrentThread(), L"GSThread");
#else
		pthread_setname_np(pthread_self(), "GSThread");
#endif
		// As MTGS::ThreadEntryPoint: default rounding (nearest, no FTZ), not whatever the creating thread had.
		FPControlRegister::SetCurrent(FPControlRegister::GetDefault());
	}

	// Runs what is queued and ends the GS thread; later commands run inline. Called on the main thread at the child
	// window's WM_DESTROY, which the runner reaches only after joining the game thread (the producer): the join
	// orders every post before this one. Not a thread_local destructor at the game thread's exit: that runs under
	// the loader lock, and the GS thread's own exit then waits for it (deadlock, seen in ob_pcsx2_gs_adapter_tests).
	void StopGsThread(const char* why)
	{
		if (s_mode != Mode::Thread || !s_ring || !s_ring->running())
			return;
		s_ring->stop();
		s_mode = Mode::Inline;
		const obgs::Ring::Stats& st = s_ring->stats();
		std::fprintf(stderr, "[pcsx2-gs] gs thread stopped (%s): %llu records, %llu sync waits\n", why,
			(unsigned long long)st.records, (unsigned long long)st.syncWaits);
		std::fflush(stderr);
	}

	// True: post to the ring. Decides once, on the first ob_gs_* command (env PS2X_GS_THREAD, default on).
	bool Threaded()
	{
		if (s_mode == Mode::Undecided)
		{
			const char* e = std::getenv("PS2X_GS_THREAD");
			s_mode = (e && e[0] == '0' && e[1] == 0) ? Mode::Inline : Mode::Thread;
			if (s_mode == Mode::Thread)
			{
				s_ring = new obgs::Ring();
				if (!s_ring->start(&RingHandler, &RingThreadInit, nullptr, kRingBytes, kQueueAheadVsyncs))
					s_mode = Mode::Inline;
			}
			std::fprintf(stderr, "[pcsx2-gs] gs thread %s (PS2X_GS_THREAD=%s; ring %zu MiB, queue ahead %d vsyncs)\n",
				s_mode == Mode::Thread ? "on" : "off", e ? e : "unset", kRingBytes >> 20, kQueueAheadVsyncs);
			std::fflush(stderr);
		}
		if (s_mode != Mode::Thread)
			return false;
		const DWORD tid = GetCurrentThreadId();
		if (tid != s_producer_tid)
		{
			// One producer at a time (the ring is SPSC): a hand-over (startup on the main thread, then the game thread)
			// is fine; two threads posting at once is not. Logged so a second producer shows in the boot log.
			if (s_producer_tid)
			{
				std::fprintf(stderr, "[pcsx2-gs] ring producer thread %lu -> %lu\n", s_producer_tid, tid);
				std::fflush(stderr);
			}
			s_producer_tid = tid;
		}
		return true;
	}
} // namespace

void ob_gs_gif(uint32_t path, const uint8_t* data, uint32_t size_bytes)
{
	if (Threaded())
	{
		if (size_bytes >= 16) // ExecGif ignores anything shorter: not worth a record
			s_ring->post(kGif, 0, path, 0, 0, data, size_bytes);
		return;
	}
	std::lock_guard<std::mutex> lk(s_mu);
	ExecGif(path, data, size_bytes);
}

void ob_gs_priv_write(uint32_t offset, uint64_t value)
{
	if (Threaded())
	{
		s_ring->post(kPriv, 0, offset, value, 0, nullptr, 0);
		return;
	}
	std::lock_guard<std::mutex> lk(s_mu);
	ExecPriv(offset, value);
}

void ob_gs_vsync(const uint64_t priv[15], uint64_t csr, uint64_t imr, uint64_t busdir, uint64_t siglblid)
{
	if (Threaded())
	{
		VsyncArgs v;
		std::memcpy(v.priv, priv, sizeof(v.priv));
		v.csr = csr;
		v.imr = imr;
		v.busdir = busdir;
		v.siglblid = siglblid;
		s_ring->post(kVsync, obgs::Ring::kVsync, 0, 0, 0, &v, sizeof(v)); // stalls here past the queue-ahead limit
		if (++s_vsyncs_posted % 1800 == 0)
		{
			const obgs::Ring::Stats& st = s_ring->stats();
			std::fprintf(stderr, "[pcsx2-gs] gs thread: %llu vsyncs, %llu records, %llu MiB, sync waits %llu (%.1f ms), "
				"queue-ahead stalls %llu (%.1f ms), ring full %llu, indirect %llu, timeout wakes %llu\n", (unsigned long long)s_vsyncs_posted,
				(unsigned long long)st.records, (unsigned long long)(st.bytes >> 20), (unsigned long long)st.syncWaits,
				st.syncWaitNs / 1e6, (unsigned long long)st.vsyncStalls, st.vsyncStallNs / 1e6,
				(unsigned long long)st.fullStalls, (unsigned long long)st.indirect, (unsigned long long)s_ring->timeoutWakes());
			std::fflush(stderr);
		}
		return;
	}
	std::lock_guard<std::mutex> lk(s_mu);
	ExecVsync(priv, csr, imr, busdir, siglblid);
}

uint32_t ob_gs_read_local(uint8_t* dst, uint32_t max_bytes)
{
	if (Threaded())
	{
		// MTGS::InitAndReadFIFO: SendPointerPacket + WaitGS. Every earlier draw executes first.
		u32 result = 0;
		s_ring->call(kReadLocal, max_bytes, reinterpret_cast<u64>(dst), reinterpret_cast<u64>(&result));
		return result;
	}
	std::lock_guard<std::mutex> lk(s_mu);
	return ExecReadLocal(dst, max_bytes);
}

void ob_gs_reset()
{
	if (Threaded())
	{
		s_ring->post(kReset, 0, 0, 0, 0, nullptr, 0);
		return;
	}
	std::lock_guard<std::mutex> lk(s_mu);
	ExecReset();
}

const uint8_t* ob_gs_snapshot(uint32_t* width, uint32_t* height)
{
	if (Threaded())
	{
		SnapshotResult r{nullptr, 0, 0};
		s_ring->call(kSnapshot, 0, reinterpret_cast<u64>(&r), 0);
		if (r.p)
		{
			*width = r.w;
			*height = r.h;
		}
		return r.p; // s_rgba: only the next snapshot (posted by this same thread) rewrites it
	}
	std::lock_guard<std::mutex> lk(s_mu);
	return ExecSnapshot(width, height);
}

int ob_gs_threaded()
{
	return s_mode == Mode::Thread && s_ring && s_ring->running() ? 1 : 0;
}
