// ob_ui: the settings menu's screens (docs/plans/2026-10-26-settings-menu.md): the launcher before the game and the
// menu over it, drawn with Dear ImGui on PCSX2's GS (ob_gs_ui_present). Plain types only: the caller
// (native/pcsx2_gs/adapter/hook.cpp, PS2Recomp patch 0078) samples raylib's input and does the window calls, so this
// library sees neither raylib nor the runtime.
#pragma once

#include <cstdint>
#include <string>

// Key codes in ObUiInput::keys: navigation keys and gamepad buttons, mapped to ImGui keys by ob_ui.
namespace ObUiNavKey
{
	enum : int { Tab = 1, Left, Right, Up, Down, PageUp, PageDown, Home, End, Delete, Backspace, Space, Enter };
}
namespace ObUiPadKey
{
	enum : int { Start = 100, Back, FaceLeft, FaceRight, FaceUp, FaceDown, DpadLeft, DpadRight, DpadUp, DpadDown, L1, R1,
		L2, R2, L3, R3 };
}

// One frame of host input, sampled on the main thread after raylib polled it.
struct ObUiInput
{
	double time = 0.0;          // seconds, monotonic
	int width = 0, height = 0;  // window client size
	float mouseX = -1.0f, mouseY = -1.0f;
	bool mouseDown[3] = {};
	float wheel = 0.0f;
	// Navigation key and gamepad button changes this frame (ObUiNavKey / ObUiPadKey codes).
	struct Key
	{
		int code;
		bool down;
	};
	Key keys[96] = {};
	int keyCount = 0;
	bool ctrl = false, shift = false, alt = false;
	unsigned int chars[16] = {};
	int charCount = 0;
	bool padConnected = false;
	float padLeftX = 0.0f, padLeftY = 0.0f; // left stick, -1..1
	// The menu trigger's physical inputs (gamepad 0 Back/Start/Guide, Windows: Back + Start held 5 s toggles; Android: no
	// combo; F1/Esc presses this frame, Android's Back button reported as Esc).
	bool backDown = false, startDown = false, guideDown = false, f1Pressed = false, escPressed = false;
	// Binding capture: the key / gamepad button first pressed this frame, as its input.ini name ("" = none).
	std::string keyPressedName, padPressedName;
	// Held this frame, for the menu's own step-in navigation (ob_ui does the edges, repeat and stick hysteresis).
	struct Held
	{
		bool up = false, down = false, left = false, right = false, a = false, b = false, y = false, l1 = false,
			 r1 = false, start = false;
	};
	Held pad;   // gamepad 0: D-pad, A (bottom face), B (right), Y (top), LB, RB, Start
	Held keys2; // keyboard: arrows, Enter/Space = a, Esc/Backspace = b, Tab = r1 (Shift+Tab = l1)
	bool mouseMoved = false; // the mouse moved, clicked or scrolled this frame (switches the menu to mouse mode)
};

// What the menu needs from its host. Function pointers may be null (that feature is then hidden).
struct ObUiHost
{
	std::string settingsFile; // outbreak.ini the runner reads (the menu writes it)
	std::string inputFile;    // input.ini the runner reads
	std::string filesDir;     // Android: the app's external files dir (drivers/, textures/, mods/); else the settings folder
	std::string gameSerial;   // for texture / mods folders (SLPM-65428)
	bool android = false;
	void (*setWindowMode)(int mode) = nullptr; // 0 windowed, 1 borderless, 2 fullscreen
	void (*setWindowSize)(int w, int h) = nullptr;
	void (*setVolume)(float volume) = nullptr; // 0..1
	void (*reloadInputBindings)() = nullptr;    // after input.ini changed (the game is not reading the pad then)
	void (*setTitle)(const char* title) = nullptr;
};

namespace obui
{
	void init(const ObUiHost& host);

	// Start-up settings that apply without a window (window mode, volume, vsync, FPS readout). Call once, before the
	// launcher or the game. gamePath: the runner's game (command line or settings); on Android a saved disc document
	// replaces it (its file descriptor path). True = boot it without the launcher: interactive launcher off and the
	// game passes the disc check.
	bool prepare(std::string& gamePath, bool interactive);

	// One launcher frame. 0 = keep going, 1 = Play (gamePath = the verified game), 2 = Quit.
	int launcherFrame(const ObUiInput& in, std::string& gamePath);

	// True once after the launcher changed a setting the GS opens with (resolution, scaling, textures, mods): the
	// caller reopens the GS (ob_gs_reopen) before the game's first frame.
	bool gsOptionsChanged();

	// One host frame while the game runs: 0 = run, 1 = hold (the menu is open), 2 = quit.
	int hostFrame(const ObUiInput& in);

	// Game thread: hides buttons (menu combo hold-back; everything while the menu is open). Active-low PS2 mask.
	uint16_t filterPad(int port, uint16_t buttons, uint8_t sticks[4], double now);
} // namespace obui
