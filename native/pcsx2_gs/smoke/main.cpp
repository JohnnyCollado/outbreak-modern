// ob_pcsx2_gs_smoke: proves ob_pcsx2_gs links and runs on its own.
// (a) SW renderer (13): one GIF PACKED packet (FRAME, SCISSOR, TEST, ZBUF, PRIM sprite, RGBAQ, XYZ2 x2:
//     a full-frame clear sprite, then a smaller sprite), PCRTC regs in basemem, GSvsync,
//     GSSaveSnapshotToMemory; checks the sprite colour inside the rectangle and the clear colour outside.
// (b) VK renderer (14): GSopen on a hidden Win32 window (device + shaders via shaderc_shared.dll), GSclose.
// Exit 0 only if (a) and (b) both pass. One line per check.
#include "common/RedtapeWindows.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/WindowInfo.h"

#include "Config.h"
#include "GS/GS.h"

#include "ob_pcsx2_gs.h"

#include <cstdio>
#include <cstring>
#include <vector>

static HWND s_hwnd;

static bool CreateHiddenWindow()
{
	// Like pcsx2-gsrunner/Main.cpp CreatePlatformWindow, minus ShowWindow (hidden).
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"ObPcsx2GsSmoke";
	if (!RegisterClassExW(&wc))
		return false;
	s_hwnd = CreateWindowExW(0, wc.lpszClassName, L"ob_pcsx2_gs_smoke", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
		CW_USEDEFAULT, 640, 448, nullptr, nullptr, wc.hInstance, nullptr);
	if (!s_hwnd)
		return false;
	RECT rc = {};
	GetClientRect(s_hwnd, &rc);
	WindowInfo wi;
	wi.type = WindowInfo::Type::Win32;
	wi.window_handle = s_hwnd;
	wi.surface_width = static_cast<u32>(rc.right - rc.left);
	wi.surface_height = static_cast<u32>(rc.bottom - rc.top);
	wi.surface_scale = 1.0f;
	ObPcsx2GS::SetRenderWindow(wi);
	return true;
}

// Reference-manifest settings (plan ## Interfaces).
static Pcsx2Config::GSOptions MakeOptions(GSRendererType r)
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
	o.DisableShaderCache = true; // compile every shader through shaderc; no cache files
	return o;
}

static void Put64(u8* p, u64 v) { std::memcpy(p, &v, 8); }

// A+D qword: data, register address.
static void AD(std::vector<u64>& q, u64 reg, u64 data)
{
	q.push_back(data);
	q.push_back(reg);
}

static constexpr u32 FB_W = 640, FB_H = 448;
static constexpr u32 CLEAR_RGB = 0x203040; // R=0x40 G=0x30 B=0x20 as 0xBBGGRR
static constexpr u32 SPRITE_RGB = 0x10E0F0; // R=0xF0 G=0xE0 B=0x10
static constexpr u32 SX0 = 160, SY0 = 112, SX1 = 480, SY1 = 336;

static u64 RGBAQ(u32 bgr) { return (u64)(bgr & 0xFF) | ((u64)((bgr >> 8) & 0xFF) << 8) | ((u64)((bgr >> 16) & 0xFF) << 16) | (0x80ull << 24) | (0x3F800000ull << 32); }
static u64 XYZ2(u32 x, u32 y) { return (u64)(x << 4) | ((u64)(y << 4) << 16); }

static bool RunSW()
{
	alignas(64) static u8 basemem[0x2000] = {};
	Put64(basemem + 0x00, 0x1 | (1u << 2) | (1u << 5) | (0xFFu << 8)); // PMODE: EN1, CRTMD=1, MMOD=ALP, ALP=0xFF
	Put64(basemem + 0x10, 2ull << 13);                                  // SMODE1: CMOD=2 (NTSC)
	Put64(basemem + 0x20, 0x1);                                         // SMODE2: INT=1, FFMD=0
	Put64(basemem + 0x70, (u64)(FB_W / 64) << 9);                       // DISPFB1: FBP=0, FBW=10, PSMCT32
	Put64(basemem + 0x80, 636ull | (50ull << 12) | (3ull << 23) | ((u64)(FB_W * 4 - 1) << 32) | ((u64)(FB_H - 1) << 44)); // DISPLAY1

	std::vector<u64> q;
	AD(q, 0x4C, (u64)(FB_W / 64) << 16);                                         // FRAME_1: FBP 0, FBW 10, PSMCT32
	AD(q, 0x40, ((u64)(FB_W - 1) << 16) | ((u64)(FB_H - 1) << 48));              // SCISSOR_1
	AD(q, 0x47, (1ull << 16) | (1ull << 17));                                    // TEST_1: ZTE, ZTST=ALWAYS
	AD(q, 0x4E, 1ull << 32);                                                     // ZBUF_1: ZMSK
	AD(q, 0x00, 6);                                                              // PRIM: sprite
	AD(q, 0x01, RGBAQ(CLEAR_RGB));
	AD(q, 0x05, XYZ2(0, 0));
	AD(q, 0x05, XYZ2(FB_W, FB_H));
	AD(q, 0x01, RGBAQ(SPRITE_RGB));
	AD(q, 0x05, XYZ2(SX0, SY0));
	AD(q, 0x05, XYZ2(SX1, SY1));
	const u64 nloop = q.size() / 2;
	std::vector<u64> pkt = {nloop | (1ull << 15) | (1ull << 60), 0xE}; // GIFtag: NLOOP, EOP, PACKED, NREG=1, REGS=A+D
	pkt.insert(pkt.end(), q.begin(), q.end());

	if (!GSopen(MakeOptions(GSRendererType::SW), GSRendererType::SW, basemem, GSVSyncMode::Disabled, false))
	{
		std::printf("SW GSopen FAILED\n");
		return false;
	}
	std::printf("SW GSopen ok\n");
	GSgifTransfer(reinterpret_cast<const u8*>(pkt.data()), static_cast<u32>(pkt.size() / 2));
	GSvsync(1, true);

	u32 w = 0, h = 0;
	std::vector<u32> px;
	const bool snap = GSSaveSnapshotToMemory(0, 0, false, true, &w, &h, &px);
	GSclose();
	if (!snap || w == 0 || h == 0 || px.size() < (size_t)w * h)
	{
		std::printf("SW snapshot FAILED (%ux%u, %zu px)\n", w, h, px.size());
		return false;
	}
	std::printf("SW snapshot %ux%u\n", w, h);
	// Sample in snapshot coordinates scaled from the 640x448 framebuffer.
	const auto at = [&](u32 fx, u32 fy) { return px[(size_t)(fy * h / FB_H) * w + (fx * w / FB_W)] & 0xFFFFFF; };
	const u32 inside = at((SX0 + SX1) / 2, (SY0 + SY1) / 2);
	const u32 outside = at(SX0 / 2, SY0 / 2);
	const bool in_ok = inside == SPRITE_RGB, out_ok = outside == CLEAR_RGB;
	std::printf("SW inside=%06X want %06X %s\n", inside, SPRITE_RGB, in_ok ? "ok" : "FAILED");
	std::printf("SW outside=%06X want %06X %s\n", outside, CLEAR_RGB, out_ok ? "ok" : "FAILED");
	return in_ok && out_ok;
}

static bool RunVK()
{
	alignas(64) static u8 basemem[0x2000] = {};
	const bool ok = GSopen(MakeOptions(GSRendererType::VK), GSRendererType::VK, basemem, GSVSyncMode::Disabled, false);
	std::printf("VK GSopen %s\n", ok ? "ok" : "FAILED");
	if (ok)
		GSclose();
	return ok;
}

int main()
{
	EmuFolders::AppRoot = std::string(Path::GetDirectory(FileSystem::GetProgramPath()));
	EmuFolders::Resources = Path::Combine(EmuFolders::AppRoot, "resources");
	if (!CreateHiddenWindow())
	{
		std::printf("window FAILED\n");
		return 1;
	}
	const bool sw = RunSW();
	const bool vk = RunVK();
	DestroyWindow(s_hwnd);
	std::printf("smoke %s\n", (sw && vk) ? "PASS" : "FAIL");
	return (sw && vk) ? 0 : 1;
}
