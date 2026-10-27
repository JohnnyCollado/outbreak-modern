// game_screen: which touch layout the game's current screen wants (docs/plans/2026-10-26-touch-controls.md section 8,
// decisions T16 and T19), read from the game's own memory: EE RAM as PS2Recomp patch 0081 hands it to the settings UI
// (PS2ExternalUi::attachGuestRam). SLPM-65428 addresses, measured on Windows (runs menu1, menu3) and in the CC0
// Outbreak decomp:
//   0x002F8654 u32 = 0x005CD580 while the game task (GAME.BIN) runs (gate/jsbar.script); before it: logos, title,
//                    scenario / character select, which all want menu controls
//   0x0048BF7E u8  = GameState +0x1E: 1 while the status screen (Start), the map (triangle) or the pause menu (Select)
//                    is open, 0 in play, in movies and in-engine scenes
//   0x003065BC u8  = Options +0x2C: the game's controller type (its pause menu's "TYPE A"...), 0..5 (main/ctrl.c:210)
// Read on request (the overlay polls about ten times a second); nothing runs per frame.
#pragma once

#include <atomic>
#include <cstdint>

namespace obui
{
	struct GameScreen
	{
		bool known = false;     // RAM attached
		bool menu = false;      // a menu screen: the D-pad layout
		uint8_t controlType = 0;
	};

	constexpr uint32_t kGameTaskSlot = 0x002F8654u;
	constexpr uint32_t kGameTaskFunc = 0x005CD580u;
	constexpr uint32_t kMenuOpen = 0x0048BF7Eu;
	constexpr uint32_t kControlType = 0x003065BCu;

	// What the screen in this RAM wants. ram may be null (not attached: unknown).
	GameScreen classifyScreen(const uint8_t* ram, uint32_t size);

	// The process's guest RAM view (ui_hook.cpp sets it from attachGuestRam) and the current screen from it.
	void attachGuestRam(const uint8_t* ram, uint32_t size);
	GameScreen currentScreen();
} // namespace obui
