// ui_hook: the PS2Recomp half of the settings UI (PS2Recomp patch 0078, runtime/ps2_ui_external.h). Feeds raylib's
// input to native/ob_ui (ob_ui.h), does its window calls, and runs its launcher loop in the runner's window.
#include "runtime/ps2_game_source.h"
#include "runtime/ps2_input_bindings.h"
#include "runtime/ps2_ui_external.h"

#include "ob_gs_bridge.h"
#include "game_screen.h"
#include "ob_ui.h"

#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(__ANDROID__)
#include <android/input.h>
#include <android_native_app_glue.h>
#include <atomic>
#include <mutex>
#include <set>
extern "C" struct android_app* GetAndroidApp(void);

namespace
{
	// raylib 5.5 routes every key from a gamepad-class device to its gamepad table and drops the ones it does not
	// map (rcore_android.c AndroidInputCallback: GAMEPAD_BUTTON_UNKNOWN -> return 1), so the Odin's own Back button
	// never reaches GetKeyPressed; the on-screen Back (a keyboard-class key) does. Catch Back from any device here,
	// ahead of raylib, and count it as the menu key (plan P4).
	std::atomic<int> s_backPresses{0};
	int32_t (*s_raylibInput)(android_app*, AInputEvent*) = nullptr;

	int32_t OnInput(android_app* app, AInputEvent* e)
	{
		if (AInputEvent_getType(e) == AINPUT_EVENT_TYPE_KEY)
		{
			const int32_t code = AKeyEvent_getKeyCode(e);
			if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN)
			{
				// One line per key code and device class seen, to learn what a handheld's extra buttons send.
				static std::mutex mu;
				static std::set<long long> seen;
				std::lock_guard<std::mutex> lk(mu);
				const long long id = (static_cast<long long>(code) << 32) | static_cast<uint32_t>(AInputEvent_getSource(e));
				if (seen.size() < 64 && seen.insert(id).second)
					std::fprintf(stderr, "[ui] key code %d source 0x%x\n", code, AInputEvent_getSource(e));
			}
			if (code == AKEYCODE_BACK)
			{
				if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(e) == 0)
					s_backPresses.fetch_add(1);
				return 1; // ours: raylib would drop or eat it anyway
			}
		}
		return s_raylibInput ? s_raylibInput(app, e) : 0;
	}

	// raylib installs its callback when the window starts; ours wraps it (checked each frame, a pointer compare).
	void HookBackKey()
	{
		android_app* app = GetAndroidApp();
		if (app && app->onInputEvent && app->onInputEvent != &OnInput)
		{
			s_raylibInput = app->onInputEvent;
			app->onInputEvent = &OnInput;
		}
	}
} // namespace
#endif

namespace
{
	// UTF-8 text of a path (C++20: u8string() is std::u8string).
	std::string U8(const std::filesystem::path& p)
	{
		const std::u8string u = p.u8string();
		return std::string(u.begin(), u.end());
	}

	struct Named
	{
		const char* name;
		int value;
	};
	// The names input.ini uses (PS2Recomp ps2_input_bindings.cpp kKeys / kPadButtons), for the binding screen.
#define OB_KEY(n) {#n, KEY_##n}
	const Named kKeyNames[] = {
		OB_KEY(APOSTROPHE), OB_KEY(COMMA), OB_KEY(MINUS), OB_KEY(PERIOD), OB_KEY(SLASH), OB_KEY(ZERO),
		OB_KEY(ONE), OB_KEY(TWO), OB_KEY(THREE), OB_KEY(FOUR), OB_KEY(FIVE), OB_KEY(SIX), OB_KEY(SEVEN),
		OB_KEY(EIGHT), OB_KEY(NINE), OB_KEY(SEMICOLON), OB_KEY(EQUAL), OB_KEY(A), OB_KEY(B), OB_KEY(C),
		OB_KEY(D), OB_KEY(E), OB_KEY(F), OB_KEY(G), OB_KEY(H), OB_KEY(I), OB_KEY(J), OB_KEY(K), OB_KEY(L),
		OB_KEY(M), OB_KEY(N), OB_KEY(O), OB_KEY(P), OB_KEY(Q), OB_KEY(R), OB_KEY(S), OB_KEY(T), OB_KEY(U),
		OB_KEY(V), OB_KEY(W), OB_KEY(X), OB_KEY(Y), OB_KEY(Z), OB_KEY(LEFT_BRACKET), OB_KEY(BACKSLASH),
		OB_KEY(RIGHT_BRACKET), OB_KEY(GRAVE), OB_KEY(SPACE), OB_KEY(ESCAPE), OB_KEY(ENTER), OB_KEY(TAB),
		OB_KEY(BACKSPACE), OB_KEY(INSERT), OB_KEY(DELETE), OB_KEY(RIGHT), OB_KEY(LEFT), OB_KEY(DOWN),
		OB_KEY(UP), OB_KEY(PAGE_UP), OB_KEY(PAGE_DOWN), OB_KEY(HOME), OB_KEY(END), OB_KEY(CAPS_LOCK),
		OB_KEY(F1), OB_KEY(F2), OB_KEY(F3), OB_KEY(F4), OB_KEY(F5), OB_KEY(F6), OB_KEY(F7), OB_KEY(F8),
		OB_KEY(F9), OB_KEY(F10), OB_KEY(F11), OB_KEY(F12), OB_KEY(LEFT_SHIFT), OB_KEY(LEFT_CONTROL),
		OB_KEY(LEFT_ALT), OB_KEY(RIGHT_SHIFT), OB_KEY(RIGHT_CONTROL), OB_KEY(RIGHT_ALT), OB_KEY(KP_0),
		OB_KEY(KP_1), OB_KEY(KP_2), OB_KEY(KP_3), OB_KEY(KP_4), OB_KEY(KP_5), OB_KEY(KP_6), OB_KEY(KP_7),
		OB_KEY(KP_8), OB_KEY(KP_9), OB_KEY(KP_DECIMAL), OB_KEY(KP_DIVIDE), OB_KEY(KP_MULTIPLY),
		OB_KEY(KP_SUBTRACT), OB_KEY(KP_ADD), OB_KEY(KP_ENTER)};
#undef OB_KEY
#define OB_PADB(n) {#n, GAMEPAD_BUTTON_##n}
	const Named kPadNames[] = {
		OB_PADB(LEFT_FACE_UP), OB_PADB(LEFT_FACE_RIGHT), OB_PADB(LEFT_FACE_DOWN), OB_PADB(LEFT_FACE_LEFT),
		OB_PADB(RIGHT_FACE_UP), OB_PADB(RIGHT_FACE_RIGHT), OB_PADB(RIGHT_FACE_DOWN), OB_PADB(RIGHT_FACE_LEFT),
		OB_PADB(LEFT_TRIGGER_1), OB_PADB(LEFT_TRIGGER_2), OB_PADB(RIGHT_TRIGGER_1), OB_PADB(RIGHT_TRIGGER_2),
		OB_PADB(MIDDLE_LEFT), OB_PADB(MIDDLE), OB_PADB(MIDDLE_RIGHT), OB_PADB(LEFT_THUMB), OB_PADB(RIGHT_THUMB)};
#undef OB_PADB

	// raylib key / gamepad button -> ObUiInput::Key code (ob_ui.h ObUiNavKey, ObUiPadKey).
	struct KeyMap
	{
		int raylib;
		int code;
	};
	const KeyMap kNavKeys[] = {{KEY_TAB, ObUiNavKey::Tab}, {KEY_LEFT, ObUiNavKey::Left}, {KEY_RIGHT, ObUiNavKey::Right},
		{KEY_UP, ObUiNavKey::Up}, {KEY_DOWN, ObUiNavKey::Down}, {KEY_PAGE_UP, ObUiNavKey::PageUp},
		{KEY_PAGE_DOWN, ObUiNavKey::PageDown}, {KEY_HOME, ObUiNavKey::Home}, {KEY_END, ObUiNavKey::End},
		{KEY_DELETE, ObUiNavKey::Delete}, {KEY_BACKSPACE, ObUiNavKey::Backspace}, {KEY_SPACE, ObUiNavKey::Space},
		{KEY_ENTER, ObUiNavKey::Enter}, {KEY_KP_ENTER, ObUiNavKey::Enter}};
	const KeyMap kPadKeys[] = {{GAMEPAD_BUTTON_MIDDLE_RIGHT, ObUiPadKey::Start}, {GAMEPAD_BUTTON_MIDDLE_LEFT, ObUiPadKey::Back},
		{GAMEPAD_BUTTON_RIGHT_FACE_LEFT, ObUiPadKey::FaceLeft}, {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, ObUiPadKey::FaceRight},
		{GAMEPAD_BUTTON_RIGHT_FACE_UP, ObUiPadKey::FaceUp}, {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, ObUiPadKey::FaceDown},
		{GAMEPAD_BUTTON_LEFT_FACE_LEFT, ObUiPadKey::DpadLeft}, {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, ObUiPadKey::DpadRight},
		{GAMEPAD_BUTTON_LEFT_FACE_UP, ObUiPadKey::DpadUp}, {GAMEPAD_BUTTON_LEFT_FACE_DOWN, ObUiPadKey::DpadDown},
		{GAMEPAD_BUTTON_LEFT_TRIGGER_1, ObUiPadKey::L1}, {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, ObUiPadKey::R1},
		{GAMEPAD_BUTTON_LEFT_TRIGGER_2, ObUiPadKey::L2}, {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, ObUiPadKey::R2},
		{GAMEPAD_BUTTON_LEFT_THUMB, ObUiPadKey::L3}, {GAMEPAD_BUTTON_RIGHT_THUMB, ObUiPadKey::R3}};

	ObUiInput SampleInput()
	{
		ObUiInput in;
		in.time = GetTime();
		in.width = GetScreenWidth();
		in.height = GetScreenHeight();
		const Vector2 m = GetMousePosition();
		in.mouseX = m.x;
		in.mouseY = m.y;
		in.mouseDown[0] = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
		in.mouseDown[1] = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
		in.mouseDown[2] = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
		in.wheel = GetMouseWheelMove();
		in.ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
		in.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
		in.alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
		auto addKey = [&in](int code, bool down) {
			if (in.keyCount < static_cast<int>(sizeof(in.keys) / sizeof(in.keys[0])))
				in.keys[in.keyCount++] = ObUiInput::Key{code, down};
		};
		for (const KeyMap& k : kNavKeys)
			if (IsKeyPressed(k.raylib) || IsKeyReleased(k.raylib))
				addKey(k.code, IsKeyDown(k.raylib));
		for (int c = GetCharPressed(); c && in.charCount < 16; c = GetCharPressed())
			in.chars[in.charCount++] = static_cast<unsigned int>(c);
		// Presses from raylib's queue, not IsKeyPressed: a press and release inside one frame (a quick tap of Android's
		// Back key) leaves the key state up but stays in the queue.
#if defined(__ANDROID__)
		HookBackKey();
		in.escPressed |= s_backPresses.exchange(0) > 0; // Back from any device (the Odin's button, the screen's)
#endif
		for (int k = GetKeyPressed(); k != 0; k = GetKeyPressed())
		{
			in.f1Pressed |= k == KEY_F1;
			in.escPressed |= k == KEY_ESCAPE;
#if defined(__ANDROID__)
			in.escPressed |= k == KEY_BACK; // plan P4: the system Back button
#endif
			if (in.keyPressedName.empty())
				for (const Named& n : kKeyNames)
					if (n.value == k)
					{
						in.keyPressedName = n.name; // binding capture: the first named key pressed this frame
						break;
					}
		}
		in.keys2.up = IsKeyDown(KEY_UP);
		in.keys2.down = IsKeyDown(KEY_DOWN);
		in.keys2.left = IsKeyDown(KEY_LEFT);
		in.keys2.right = IsKeyDown(KEY_RIGHT);
		in.keys2.a = IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_KP_ENTER) || IsKeyDown(KEY_SPACE);
		in.keys2.b = IsKeyDown(KEY_ESCAPE) || IsKeyDown(KEY_BACKSPACE);
		in.keys2.r1 = IsKeyDown(KEY_TAB) && !in.shift;
		in.keys2.l1 = IsKeyDown(KEY_TAB) && in.shift;
		const Vector2 md = GetMouseDelta();
		in.mouseMoved = md.x != 0.0f || md.y != 0.0f || in.wheel != 0.0f || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
			IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
		const int pad = 0;
		in.padConnected = IsGamepadAvailable(pad);
		if (in.padConnected)
		{
			for (const KeyMap& k : kPadKeys)
				if (IsGamepadButtonPressed(pad, k.raylib) || IsGamepadButtonReleased(pad, k.raylib))
					addKey(k.code, IsGamepadButtonDown(pad, k.raylib));
			in.padLeftX = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);
			in.padLeftY = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
			in.pad.up = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP);
			in.pad.down = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
			in.pad.left = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
			in.pad.right = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
			in.pad.a = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
			in.pad.b = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
			in.pad.y = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);
			in.pad.l1 = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
			in.pad.r1 = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
			in.pad.start = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT);
			in.backDown = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_LEFT);
			in.startDown = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT);
			in.guideDown = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE);
			for (const Named& n : kPadNames)
				if (IsGamepadButtonPressed(pad, n.value))
				{
					in.padPressedName = n.name;
					break;
				}
		}
		return in;
	}

	// Window calls for ob_ui (main thread).
	bool s_borderless = false; // our own borderless (ob_gs_set_borderless), not raylib's flag
	bool s_fullscreen = false; // the monitor switched by ob_gs_set_display_mode
	void UiSetWindowMode(int mode)
	{
#if !defined(__ANDROID__)
		// Borderless and fullscreen by hand (raylib's toggles left the screen black, see the adapter): fullscreen is
		// the monitor switched to the Output resolution, then borderless over it.
		ob_gs_set_borderless(GetWindowHandle(), false);
		if (s_fullscreen)
			ob_gs_set_display_mode(GetWindowHandle(), 0, 0);
		s_fullscreen = false;
		if (mode == 2)
		{
			const ps2game::Settings& st = ps2game::activeSettings();
			const uint32_t w = st.windowWidth ? st.windowWidth : 1920u, h = st.windowHeight ? st.windowHeight : 1080u;
			s_fullscreen = ob_gs_set_display_mode(GetWindowHandle(), w, h);
		}
		s_borderless = mode != 0;
		if (mode != 0)
			ob_gs_set_borderless(GetWindowHandle(), true);
		else
			ob_gs_window_resized(static_cast<uint32_t>(GetScreenWidth()), static_cast<uint32_t>(GetScreenHeight()));
		std::fprintf(stderr, "[ui] window mode %d: %dx%d\n", mode, GetScreenWidth(), GetScreenHeight());
#else
		(void)mode;
#endif
	}
	void UiSetWindowSize(int w, int h)
	{
#if !defined(__ANDROID__)
		if (s_borderless)
			return; // applies when back in a window, and at the next start
		SetWindowSize(w, h);
		ob_gs_window_resized(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
#else
		(void)w;
		(void)h;
#endif
	}
	// The saved volume is applied at start-up (obui::prepare), before the runtime opens the audio device
	// (PS2Runtime::initialize: InitAudioDevice); raylib's SetMasterVolume does nothing without a device, and a new device
	// starts at full volume (developer, 2026-10-27: "set to zero, it'll still play"). So until the device is ready the
	// volume waits here, and the first host frame applies it.
	float s_pendingVolume = -1.0f;

	void UiSetVolume(float v)
	{
		if (IsAudioDeviceReady())
		{
			SetMasterVolume(v);
			s_pendingVolume = -1.0f;
		}
		else
			s_pendingVolume = v;
	}

	void ApplyPendingVolume()
	{
		if (s_pendingVolume < 0.0f || !IsAudioDeviceReady())
			return;
		SetMasterVolume(s_pendingVolume);
		std::fprintf(stderr, "[ui] volume %d%% applied (audio device ready)\n", static_cast<int>(s_pendingVolume * 100.0f + 0.5f));
		s_pendingVolume = -1.0f;
	}
	void UiSetTitle(const char* t) { SetWindowTitle(t); }
	std::string InputIniPath()
	{
		if (const char* env = std::getenv("PS2X_INPUT_INI"); env && *env)
			return env;
#if defined(__ANDROID__)
		return U8(ps2game::settingsDirectory() / "input.ini");
#else
		return U8(std::filesystem::u8path(GetApplicationDirectory()) / "input.ini");
#endif
	}
	void UiReloadInput()
	{
#if defined(__ANDROID__)
		// The runtime looks for input.ini beside the exe, which an app has none of: point it at the files dir.
		setenv("PS2X_INPUT_INI", InputIniPath().c_str(), 1);
#endif
		ps2input::loadInputBindingsAtStartup();
	}

	// The window icon (Windows: outbreak-icon.png beside the exe, staged by ob.sh recompile from native/windows, made
	// by tools/ob/make_icons.py). Once, as soon as the window exists; Android takes the launcher icon from the APK.
	void SetIconOnce()
	{
#if !defined(__ANDROID__)
		static bool s_done = false;
		if (s_done || !IsWindowReady())
			return;
		s_done = true;
		const std::string path = U8(std::filesystem::u8path(GetApplicationDirectory()) / "outbreak-icon.png");
		Image icon = LoadImage(path.c_str());
		if (icon.data)
		{
			SetWindowIcon(icon);
			UnloadImage(icon);
		}
		else
			std::fprintf(stderr, "[ui] no window icon at %s\n", path.c_str());
#endif
	}

	class ObExternalUi final : public PS2ExternalUi
	{
	public:
		bool launch(std::filesystem::path& gamePath, bool interactive) override
		{
			ObUiHost host;
			const ps2game::Settings& settings = ps2game::activeSettings();
			host.settingsFile =
				U8(settings.file.empty() ? ps2game::settingsDirectory() / "outbreak.ini" : settings.file);
			host.inputFile = InputIniPath();
			host.filesDir = U8(ps2game::settingsDirectory());
#if defined(__ANDROID__)
			host.android = true;
			UiReloadInput(); // input.ini in the files dir from the start
#endif
			host.setWindowMode = &UiSetWindowMode;
			host.setWindowSize = &UiSetWindowSize;
			host.setVolume = &UiSetVolume;
			host.reloadInputBindings = &UiReloadInput;
			host.setTitle = &UiSetTitle;
			obui::init(host);
			std::string game = U8(gamePath);
			const bool skip = obui::prepare(game, interactive);
			gamePath = std::filesystem::u8path(game);
			if (!interactive || skip)
				return true;
			SetWindowTitle("Outbreak Modern");
			SetIconOnce();
#if !defined(__ANDROID__)
			// No window size in the settings: the runner's built-in 640x448 is too small for the launcher. Open at
			// 1280x720 (at most 90% of the monitor), centred; the game then keeps this window.
			if (ps2game::activeSettings().windowWidth == 0 && !IsWindowFullscreen() &&
				!s_borderless)
			{
				const int mon = GetCurrentMonitor();
				const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
				const int w = mw > 0 ? std::min(1280, mw * 9 / 10) : 1280;
				const int h = mh > 0 ? std::min(720, mh * 9 / 10) : 720;
				SetWindowSize(w, h);
				if (mw > 0 && mh > 0)
				{
					const Vector2 mp = GetMonitorPosition(mon);
					SetWindowPosition(static_cast<int>(mp.x) + (mw - w) / 2, static_cast<int>(mp.y) + (mh - h) / 2);
				}
				ob_gs_window_resized(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
			}
#endif
			std::fprintf(stderr, "[ui] launcher\n");
			while (!WindowShouldClose())
			{
				BeginDrawing(); // raylib polls input in EndDrawing; the picture is the GS's child window
				ClearBackground(BLACK);
				EndDrawing();
				// The run loop forwards resizes during the game; here the launcher does (maximize, drag).
				if (IsWindowResized())
					ob_gs_window_resized(static_cast<uint32_t>(GetScreenWidth()), static_cast<uint32_t>(GetScreenHeight()));
				if (!ob_gs_window_ready())
					continue; // Android, app in the background (e.g. the file picker): nothing to draw on
				const int r = obui::launcherFrame(SampleInput(), game);
				if (r == 1)
				{
					gamePath = std::filesystem::u8path(game);
					return true;
				}
				if (r == 2)
					return false;
			}
			return false;
		}

		HostAction hostFrame() override
		{
			SetIconOnce();
			ApplyPendingVolume();
			if (!ob_gs_window_ready())
				return HostAction::Hold; // Android, app in the background: hold the game until its window is back
			const int r = obui::hostFrame(SampleInput());
			return r == 2 ? HostAction::Quit : r == 1 ? HostAction::Hold : HostAction::Run;
		}

		uint16_t filterPad(int port, uint16_t buttons, uint8_t sticks[4]) override
		{
			return obui::filterPad(port, buttons, sticks, GetTime());
		}

		// PS2Recomp patch 0081: the game's RAM, read-only, for the touch controls' layout per screen
		// (native/ob_ui/core/game_screen.h).
		void attachGuestRam(const uint8_t* rdram, uint32_t size) override { obui::attachGuestRam(rdram, size); }
	};
} // namespace

PS2ExternalUi* ps2CreateExternalUi()
{
	static ObExternalUi s_ui;
	return &s_ui;
}
