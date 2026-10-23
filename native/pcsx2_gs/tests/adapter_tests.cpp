// ob_pcsx2_gs_adapter_tests: the runner's PCSX2 adapter (adapter/pcsx2_side.cpp, the ob_gs_* bridge) end to end on
// PCSX2's SW renderer, through the GS thread by default (PS2X_GS_THREAD=0: inline, as before). From a "game thread"
// (a std::thread, as the runner's): clear + 40 sprites of changing colour over one rectangle (path 3), then a
// local->host transfer and ob_gs_read_local: every pixel must be the LAST sprite's colour (each draw executed, in
// order, before the readback); a vsync + snapshot through the same path. Then, as the runner's shutdown (join the game
// thread, CloseWindow), the main thread destroys the parent window: the GS thread must stop, later calls run inline.
// Output as ps2x_tests for ob.sh test. Runs from the pcsx2-gs build dir (resources/shaders, shaderc beside it).
#include "ob_gs_bridge.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
	int g_total = 0, g_passed = 0;
	void Check(const char* name, bool ok, const char* detail = "")
	{
		g_total++;
		if (ok)
			g_passed++;
		std::printf("[%s] %s %s\n", ok ? "Passed" : "Failed", name, detail);
		std::fflush(stdout);
	}

	using u64 = uint64_t;
	using u32 = uint32_t;
	constexpr u32 FB_W = 640, FB_H = 448;
	constexpr u32 SX0 = 160, SY0 = 112, SX1 = 480, SY1 = 336;
	constexpr u32 CLEAR_RGB = 0x203040;
	constexpr u32 RX = 256, RY = 200, RW = 64, RH = 4; // readback rect, inside the sprite

	void AD(std::vector<u64>& q, u64 reg, u64 data)
	{
		q.push_back(data);
		q.push_back(reg);
	}
	u64 RGBAQ(u32 bgr) { return (u64)(bgr & 0xFF) | ((u64)((bgr >> 8) & 0xFF) << 8) | ((u64)((bgr >> 16) & 0xFF) << 16) | (0x80ull << 24) | (0x3F800000ull << 32); }
	u64 XYZ2(u32 x, u32 y) { return (u64)(x << 4) | ((u64)(y << 4) << 16); }
	// One PACKED A+D packet (GIFtag NLOOP = pairs, EOP, NREG 1, REGS = A+D).
	void Send(const std::vector<u64>& q)
	{
		std::vector<u64> pkt = {(q.size() / 2) | (1ull << 15) | (1ull << 60), 0xE};
		pkt.insert(pkt.end(), q.begin(), q.end());
		ob_gs_gif(3, reinterpret_cast<const uint8_t*>(pkt.data()), static_cast<uint32_t>(pkt.size() * 8));
	}
	u32 SpriteColour(u32 i) { return 0x100000u * (i % 15) + 0x0800u * i + 0x11u * (i % 13); }

	void GameThread(bool& threaded_during, int& read_bad, u32& read_first, u32& read_bytes, bool& snap_ok, char* snap_detail)
	{
		std::vector<u64> q;
		AD(q, 0x4C, (u64)(FB_W / 64) << 16);                            // FRAME_1: FBP 0, FBW 10, PSMCT32
		AD(q, 0x40, ((u64)(FB_W - 1) << 16) | ((u64)(FB_H - 1) << 48)); // SCISSOR_1
		AD(q, 0x47, (1ull << 16) | (1ull << 17));                       // TEST_1: ZTE, ZTST=ALWAYS
		AD(q, 0x4E, 1ull << 32);                                        // ZBUF_1: ZMSK
		AD(q, 0x00, 6);                                                 // PRIM: sprite
		AD(q, 0x01, RGBAQ(CLEAR_RGB));
		AD(q, 0x05, XYZ2(0, 0));
		AD(q, 0x05, XYZ2(FB_W, FB_H));
		Send(q);
		const u32 sprites = 40;
		for (u32 i = 0; i < sprites; i++)
		{
			q.clear();
			AD(q, 0x01, RGBAQ(SpriteColour(i)));
			AD(q, 0x05, XYZ2(SX0, SY0));
			AD(q, 0x05, XYZ2(SX1, SY1));
			Send(q);
		}
		threaded_during = ob_gs_threaded() != 0;
		// Local -> host: BITBLTBUF (SBP 0, SBW 10, PSMCT32), TRXPOS (SSAX/SSAY), TRXREG (RRW/RRH), TRXDIR = 1.
		q.clear();
		AD(q, 0x50, (u64)(FB_W / 64) << 16);
		AD(q, 0x51, (u64)RX | ((u64)RY << 16));
		AD(q, 0x52, (u64)RW | ((u64)RH << 32));
		AD(q, 0x53, 1);
		Send(q);
		std::vector<u32> px(RW * RH, 0xDEADBEEFu);
		read_bytes = ob_gs_read_local(reinterpret_cast<uint8_t*>(px.data()), RW * RH * 4);
		const u32 want = SpriteColour(sprites - 1);
		read_bad = 0;
		for (u32 p : px)
			read_bad += (p & 0xFFFFFF) != want;
		read_first = px[0];

		// Display regs (PMODE EN1, SMODE2 INT, DISPFB1 FBW 10, DISPLAY1 640x448) through priv writes and the vsync
		// snapshot, then a snapshot: centre = last sprite, corner = clear.
		uint64_t priv[15] = {};
		priv[0] = 0x1 | (1u << 2) | (1u << 5) | (0xFFu << 8);
		priv[1] = 2ull << 13;
		priv[2] = 0x1;
		priv[7] = (u64)(FB_W / 64) << 9;
		priv[8] = 636ull | (50ull << 12) | (3ull << 23) | ((u64)(FB_W * 4 - 1) << 32) | ((u64)(FB_H - 1) << 44);
		for (u32 i = 0; i < 15; i++)
			ob_gs_priv_write(i * 0x10, priv[i]);
		for (int v = 0; v < 4; v++)
			ob_gs_vsync(priv, v & 1 ? 0x2000 : 0, 0, 0, 0);
		uint32_t w = 0, h = 0;
		const uint8_t* rgba = ob_gs_snapshot(&w, &h);
		u32 centre = 0, corner = 0;
		if (rgba && w == FB_W && h == FB_H)
		{
			std::memcpy(&centre, rgba + ((size_t)((SY0 + SY1) / 2) * w + (SX0 + SX1) / 2) * 4, 4);
			std::memcpy(&corner, rgba + ((size_t)(SY0 / 2) * w + SX0 / 2) * 4, 4);
		}
		snap_ok = rgba && (centre & 0xFFFFFF) == want && (corner & 0xFFFFFF) == CLEAR_RGB;
		std::snprintf(snap_detail, 128, "(%ux%u centre %06X want %06X, corner %06X want %06X)", w, h, centre & 0xFFFFFF,
			want, corner & 0xFFFFFF, CLEAR_RGB);
	}
} // namespace

int main()
{
	const char* e = std::getenv("PS2X_GS_THREAD");
	const bool want_thread = !(e && std::strcmp(e, "0") == 0);
	ob_gs_init(ObGsRenderer::SW);
	// The runner's window, hidden: the GS presents into a child of it (ob_gs_attach_window, main thread).
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"ObGsAdapterTests";
	RegisterClassExW(&wc);
	HWND parent = CreateWindowExW(0, wc.lpszClassName, L"ob_pcsx2_gs_adapter_tests", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 640, 448, nullptr, nullptr, wc.hInstance, nullptr);
	ob_gs_attach_window(parent, 640, 448, false);

	bool threaded_during = false, snap_ok = false;
	int read_bad = -1;
	u32 read_first = 0, read_bytes = 0;
	char snap_detail[128] = {};
	std::thread game([&] { GameThread(threaded_during, read_bad, read_first, read_bytes, snap_ok, snap_detail); });
	game.join();
	const bool threaded_after_join = ob_gs_threaded() != 0;
	DestroyWindow(parent); // the runner's CloseWindow

	char d[128];
	std::snprintf(d, sizeof(d), "(want %s)", want_thread ? "on" : "off (PS2X_GS_THREAD=0)");
	Check("mode: GS thread as PS2X_GS_THREAD asks", threaded_during == want_thread, d);
	std::snprintf(d, sizeof(d), "(%u bytes, %d of %u pixels not the last sprite, first %08X)", read_bytes, read_bad, RW * RH, read_first);
	Check("readback: after 41 queued draws, every pixel is the last sprite", read_bytes == RW * RH * 4 && read_bad == 0, d);
	Check("snapshot: through the same path, shows the last sprite on the clear", snap_ok, snap_detail);
	std::snprintf(d, sizeof(d), "(running after the join: %d, after DestroyWindow: %d)", threaded_after_join ? 1 : 0, ob_gs_threaded());
	Check("shutdown: the GS thread runs until the window goes, then stops", threaded_after_join == want_thread && ob_gs_threaded() == 0, d);
	// After the producer is gone: calls run inline and still see the same GS state.
	std::vector<u32> px(RW * RH, 0);
	const u64 trx[] = {(4ull) | (1ull << 15) | (1ull << 60), 0xE, (u64)(FB_W / 64) << 16, 0x50, (u64)RX | ((u64)RY << 16), 0x51,
		(u64)RW | ((u64)RH << 32), 0x52, 1, 0x53};
	ob_gs_gif(3, reinterpret_cast<const uint8_t*>(trx), sizeof(trx));
	const u32 n = ob_gs_read_local(reinterpret_cast<uint8_t*>(px.data()), RW * RH * 4);
	int bad = 0;
	for (u32 p : px)
		bad += (p & 0xFFFFFF) != SpriteColour(39);
	std::snprintf(d, sizeof(d), "(%u bytes, %d bad)", n, bad);
	Check("after exit: inline readback sees the same frame", n == RW * RH * 4 && bad == 0, d);

	std::printf("Passed: %d\nTotal Tests: %d\n", g_passed, g_total);
	std::fflush(stdout);
	// No GSclose (the runner never closes either): PCSX2's static destructors then fault on the open device at a
	// normal exit, in both modes. Leave as the runner's script exits do.
	std::_Exit(g_passed == g_total ? 0 : 1);
}
