// pcsx2_side: the PCSX2 half of the runner's external GS (ob_gs_bridge.h). Compiled with ob_pcsx2_gs's
// PUBLIC defines/options; never sees PS2Recomp headers. One mutex serialises every PCSX2 call.
#include "common/RedtapeWindows.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/WindowInfo.h"

#include "Config.h"
#include "GS/GS.h"

#include "ob_pcsx2_gs.h"
#include "ob_gs_bridge.h"

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
	std::vector<u32> s_px;
	std::vector<u8> s_rgba;

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
		s_open = CreateHiddenWindow() && GSopen(MakeOptions(t), t, s_basemem, GSVSyncMode::Disabled, false);
		std::fprintf(stderr, "[pcsx2-gs] open renderer=%d %s\n", static_cast<int>(t), s_open ? "ok" : "FAILED");
		std::fflush(stderr);
		return s_open;
	}

	void Put64(u32 off, u64 v) { std::memcpy(s_basemem + off, &v, 8); }
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

void ob_gs_gif(uint32_t path, const uint8_t* data, uint32_t size_bytes)
{
	static std::vector<u8> s_carry[4];
	std::lock_guard<std::mutex> lk(s_mu);
	if (!EnsureOpen() || size_bytes < 16)
		return;
	std::vector<u8>& c = s_carry[path & 3];
	const u32 n = size_bytes & ~15u;
	if (c.empty())
	{
		const u32 q = WholeUnitsQwc(data, n / 16);
		if (q)
			GSgifTransfer(data, q);
		c.assign(data + (size_t)q * 16, data + n);
		return;
	}
	c.insert(c.end(), data, data + n);
	const u32 q = WholeUnitsQwc(c.data(), (u32)(c.size() / 16));
	if (q)
		GSgifTransfer(c.data(), q);
	c.erase(c.begin(), c.begin() + (size_t)q * 16);
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
	GSvsync((csr & 0x2000) ? 1 : 0, true);
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
	u32 w = 0, h = 0;
	s_px.clear();
	if (!GSSaveSnapshotToMemory(0, 0, false, true, &w, &h, &s_px) || w == 0 || h == 0 || s_px.size() < (size_t)w * h)
		return nullptr;
	if (!s_logged_snap)
	{
		s_logged_snap = true;
		std::fprintf(stderr, "[pcsx2-gs] first snapshot %ux%u\n", w, h);
		std::fflush(stderr);
	}
	s_rgba.resize((size_t)w * h * 4);
	for (size_t i = 0; i < (size_t)w * h; i++)
	{
		const u32 p = s_px[i] | 0xFF000000u;
		std::memcpy(&s_rgba[i * 4], &p, 4);
	}
	*width = w;
	*height = h;
	return s_rgba.data();
}
