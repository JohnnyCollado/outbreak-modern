// pcsx2_side: the PCSX2 half of the runner's external GS (ob_gs_bridge.h). Compiled with ob_pcsx2_gs's
// PUBLIC defines/options; never sees PS2Recomp headers. One mutex serialises every PCSX2 call.
#include "common/RedtapeWindows.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/WindowInfo.h"

#include "Config.h"
#include "GS/GS.h"
#include "GS/Renderers/Common/GSRenderer.h"

#include "ob_pcsx2_gs.h"
#include "ob_gs_bridge.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
	std::mutex s_mu;
	ObGsRenderer s_renderer = ObGsRenderer::SW;
	bool s_tried = false, s_open = false, s_logged_snap = false;
	alignas(64) u8 s_basemem[0x2000] = {};
	std::vector<u8> s_rgba;
	HWND s_child = nullptr;   // set by ob_gs_attach_window: PCSX2 presents into this child of the runner window
	bool s_visible = false;   // parent window visible at attach: Mailbox present, else Disabled
	std::atomic<u64> s_pending_size{0}; // (w << 32) | h from ob_gs_window_resized; 0 = none; applied in ob_gs_vsync

	// Child of the runner window: no background, disabled and hit-test transparent so input stays with the parent.
	LRESULT CALLBACK ChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_NCHITTEST)
			return HTTRANSPARENT;
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

	// Same settings as smoke/main.cpp MakeOptions (reference manifest, plan ## Interfaces).
	Pcsx2Config::GSOptions MakeOptions(GSRendererType r)
	{
		Pcsx2Config::GSOptions o;
		o.Renderer = r;
		o.UpscaleMultiplier = 1.0f;
		o.InterlaceMode = GSInterlaceMode::Off;
		o.LinearPresent = GSPostBilinearMode::Off;
		o.Dithering = 2;
		o.PCRTCAntiBlur = true;
		o.FXAA = false;
		o.ShadeBoost = false;
		o.PCRTCOffsets = false;
		o.PCRTCOverscan = false;
		o.TVShader = 0;
		o.CASMode = GSCASMode::Disabled;
		o.DisableShaderCache = true;
		return o;
	}

	// Caller holds s_mu. Opens once; a failed open is not retried.
	bool EnsureOpen()
	{
		if (s_tried)
			return s_open;
		s_tried = true;
		EmuFolders::AppRoot = std::string(Path::GetDirectory(FileSystem::GetProgramPath()));
		EmuFolders::Resources = Path::Combine(EmuFolders::AppRoot, "resources");
		const GSRendererType t = ToType(s_renderer);
		const GSVSyncMode vsync = (s_child && s_visible) ? GSVSyncMode::Mailbox : GSVSyncMode::Disabled;
		s_open = (s_child || CreateHiddenWindow()) && GSopen(MakeOptions(t), t, s_basemem, vsync, false);
		std::fprintf(stderr, "[pcsx2-gs] open renderer=%d %s window=%s vsync=%d\n", static_cast<int>(t),
			s_open ? "ok" : "FAILED", s_child ? "child" : "hidden", static_cast<int>(vsync));
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

void ob_gs_gif(uint32_t path, const uint8_t* data, uint32_t size_bytes)
{
	static std::vector<u8> s_carry[4];
	std::lock_guard<std::mutex> lk(s_mu);
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

void ob_gs_priv_write(uint32_t offset, uint64_t value)
{
	std::lock_guard<std::mutex> lk(s_mu);
	if (EnsureOpen() && offset + 8 <= sizeof(s_basemem))
		Put64(offset, value);
}

void ob_gs_vsync(const uint64_t priv[15], uint64_t csr, uint64_t imr, uint64_t busdir, uint64_t siglblid)
{
	std::lock_guard<std::mutex> lk(s_mu);
	if (!EnsureOpen())
		return;
	for (u32 i = 0; i < 15; i++)
		Put64(i * 0x10, priv[i]);
	Put64(0x1000, csr);
	Put64(0x1010, imr);
	Put64(0x1040, busdir);
	Put64(0x1080, siglblid);
	if (const u64 size = s_pending_size.exchange(0))
		GSResizeDisplayWindow(static_cast<u32>(size >> 32), static_cast<u32>(size), 1.0f);
	GSvsync((csr & 0x2000) ? 1 : 0, true);
}

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
	std::fprintf(stderr, "[pcsx2-gs] attach child %ux%u visible=%d\n", w, h, visible ? 1 : 0);
	std::fflush(stderr);
}

void ob_gs_window_resized(uint32_t w, uint32_t h)
{
	if (!s_child || w == 0 || h == 0)
		return;
	SetWindowPos(s_child, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(h), SWP_NOZORDER | SWP_NOACTIVATE);
	s_pending_size.store((static_cast<u64>(w) << 32) | h);
}

uint32_t ob_gs_read_local(uint8_t* dst, uint32_t max_bytes)
{
	std::lock_guard<std::mutex> lk(s_mu);
	const u32 qwc = max_bytes / 16;
	if (!EnsureOpen() || qwc == 0)
		return 0;
	GSInitAndReadFIFO(dst, qwc);
	return qwc * 16;
}

void ob_gs_reset()
{
	std::lock_guard<std::mutex> lk(s_mu);
	if (s_open)
		GSreset(false);
}

const uint8_t* ob_gs_snapshot(uint32_t* width, uint32_t* height)
{
	std::lock_guard<std::mutex> lk(s_mu);
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
