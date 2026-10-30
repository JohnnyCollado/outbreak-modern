// settings_ui: the launcher and the in-game menu (docs/plans/2026-10-26-settings-menu.md), Dear ImGui on PCSX2's GS.
// Everything here runs on the main thread except filterPad (the game thread; MenuTrigger locks). The ImGui calls
// happen only inside Build, which ob_gs_ui_present runs under the GS lock, and only while the game is not drawing.
// Settings are saved as they change: outbreak.ini (the runner's keys at the top, the menu's own under [ui], which the
// runner skips: PS2Recomp patch 0079) and input.ini.
//
// Look (decision A15, the 2026-10-26 mockup): a header with the game line, a page list with icons, settings in rounded
// cards, Quit and an accent Play in the footer; Windows' Segoe UI and Segoe Fluent Icons, loaded from the system.
// Navigation: mouse first; a controller or the keyboard uses the developer's "step in" model instead of ImGui's own
// spatial navigation: focus starts on the page list (up/down and L1/R1 preview pages, down past the last page reaches
// the footer's Quit / Play), A steps into the page (up/down rows, left/right change the row's value, A acts), B steps
// back out. Held directions repeat and accelerate, and the stick has a dead zone with hysteresis, as PlayFieldPortal's
// GamepadInputHandler (github.com/JohnnyC96 PlayFieldPortal, feature-xmb/gamepad; the developer's own project). The
// focus highlight and the button prompts show only after controller or keyboard input; the mouse hides them again.
#include "ob_ui.h"

#include "disc_check.h"
#include "file_dialog.h"
#include "ini_file.h"
#include "memory_card.h"
#include "widescreen.h"
#include "menu_trigger.h"
#include "net_choice.h"
#include "window_size.h"
#include "touch_pad.h"
#if !defined(__ANDROID__)
#include "../../ob_server/src/host.h" // the built-in host and nearby search (by path: PCSX2 has a Host.h); Windows only
#endif

#include "ob_gs_bridge.h"

#include "imgui.h"
#include "imgui_internal.h"

#if defined(__ANDROID__)
#include "android_menu.h"
#include "android_picker.h"
#include <android/api-level.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	// UTF-8 text of a path (C++20: u8string() is std::u8string).
	std::string U8(const fs::path& p)
	{
		const std::u8string u = p.u8string();
		return std::string(u.begin(), u.end());
	}

	// UTF-8 of one code point (the icon font's private-use glyphs).
	std::string Glyph(unsigned cp)
	{
		std::string out;
		if (cp < 0x80)
			out += static_cast<char>(cp);
		else if (cp < 0x800)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		return out;
	}

	// Icon + text centred on one middle line (developer, 2026-10-26: "center align ... their icon and text,
	// vertically"). Segoe Fluent Icons' glyphs sit higher and taller than Segoe UI's letters, so each is placed by its
	// own ink at the current font and size (the baked glyph's Y0..Y1 from the line top), not by the shared line top:
	// the icon by its own box, the text by a capital's. Returns the y to pass to AddText.
	float InkTop(unsigned cp, float midY)
	{
		ImFontGlyph* g = ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(cp));
		return std::floor(g ? midY - (g->Y0 + g->Y1) * 0.5f : midY - ImGui::GetFontSize() * 0.5f);
	}

	float IconWidth() { return ImGui::GetFontSize() * 1.7f; }

	// Draws [icon][text] from x, both centred on midY; icon 0 = text only. Returns the width drawn.
	float IconText(ImDrawList* dl, float x, float midY, unsigned icon, const char* text, ImU32 col)
	{
		const float x0 = x;
		if (icon)
		{
			dl->AddText(ImVec2(x, InkTop(icon, midY)), col, Glyph(icon).c_str());
			x += IconWidth();
		}
		if (text && *text)
		{
			dl->AddText(ImVec2(x, InkTop('H', midY)), col, text);
			x += ImGui::CalcTextSize(text).x;
		}
		return x - x0;
	}

	float IconTextWidth(unsigned icon, const char* text)
	{
		return (icon ? IconWidth() : 0.0f) + (text && *text ? ImGui::CalcTextSize(text).x : 0.0f);
	}

	enum class Page : int
	{
		Game,
		Controls,
		Camera,
		Display,
		Mods,
		Cards,
		Audio,
		Network,
		Driver,
		Advanced,
		Count,
	};
	const char* const kPageNames[] = {"Game", "Controls", "Camera", "Display", "Mods", "Memory cards", "Audio", "Network",
		"Graphics driver", "Advanced"};
	// Segoe Fluent Icons / Segoe MDL2 Assets: Home, Game, Camera, TVMonitor, Puzzle, SDCard, Volume, Wifi, Component,
	// Setting.
	const unsigned kPageIcons[] = {0xE80F, 0xE7FC, 0xE722, 0xE7F4, 0xEA86, 0xE7F1, 0xE767, 0xE701, 0xE950, 0xE713};

	// input.ini rows: key, label, keyboard default, gamepad default (PCSX2's mappings, native/input.ini.example).
	struct BindRow
	{
		const char* key;
		const char* label;
		const char* keyboard;
		const char* gamepad;
	};
	const BindRow kBindRows[] = {
		{"cross", "Cross", "K", "RIGHT_FACE_DOWN"},
		{"circle", "Circle", "L", "RIGHT_FACE_RIGHT"},
		{"square", "Square", "J", "RIGHT_FACE_LEFT"},
		{"triangle", "Triangle", "I", "RIGHT_FACE_UP"},
		{"l1", "L1", "Q", "LEFT_TRIGGER_1"},
		{"r1", "R1", "E", "RIGHT_TRIGGER_1"},
		{"l2", "L2", "ONE", "LEFT_TRIGGER_2, LEFT_TRIGGER"},
		{"r2", "R2", "THREE", "RIGHT_TRIGGER_2, RIGHT_TRIGGER"},
		{"l3", "L3", "TWO", "LEFT_THUMB"},
		{"r3", "R3", "FOUR", "RIGHT_THUMB"},
		{"start", "Start", "ENTER", "MIDDLE_RIGHT"},
		{"select", "Select", "BACKSPACE", "MIDDLE_LEFT"},
		{"up", "D-pad up", "UP", "LEFT_FACE_UP"},
		{"down", "D-pad down", "DOWN", "LEFT_FACE_DOWN"},
		{"left", "D-pad left", "LEFT", "LEFT_FACE_LEFT"},
		{"right", "D-pad right", "RIGHT", "LEFT_FACE_RIGHT"},
	};
	// Keyboard stick rows: one of the four names in stick_left / stick_right ("up, down, left, right").
	struct StickRow
	{
		const char* key;
		int index;
		const char* label;
		const char* def;
	};
	const StickRow kStickRows[] = {
		{"stick_left", 0, "Left stick up", "W"}, {"stick_left", 1, "Left stick down", "S"},
		{"stick_left", 2, "Left stick left", "A"}, {"stick_left", 3, "Left stick right", "D"},
		{"stick_right", 0, "Right stick up", "T"}, {"stick_right", 1, "Right stick down", "G"},
		{"stick_right", 2, "Right stick left", "F"}, {"stick_right", 3, "Right stick right", "H"},
	};

	struct Resolution
	{
		int w, h;
		const char* label;
	};
	const Resolution kWindowSizes[] = {{1280, 720, "1280 x 720"}, {1600, 900, "1600 x 900"}, {1920, 1080, "1920 x 1080"},
		{2560, 1440, "2560 x 1440"}, {3840, 2160, "3840 x 2160"}};
	const int kUpscales[] = {1, 2, 3, 4, 6};
	const char* const kUpscaleNames[] = {"Native (1x)", "720p (2x)", "1080p (3x)", "1440p (4x)", "4K (6x)"};

	// Colours (dark, close to the mockup): page, card, raised control, accent, text.
	const ImU32 kCardBg = IM_COL32(32, 36, 44, 255);
	const ImU32 kFocusBg = IM_COL32(59, 130, 246, 46);
	const ImU32 kFocusEdge = IM_COL32(96, 165, 250, 200);
	const ImVec4 kAccent(0.231f, 0.510f, 0.965f, 1.0f);
	const ImVec4 kAccentHover(0.322f, 0.580f, 0.980f, 1.0f);
	const ImVec4 kSuccess(0.204f, 0.827f, 0.600f, 1.0f);
	const ImVec4 kWarn(0.984f, 0.749f, 0.141f, 1.0f);
	const ImVec4 kDanger(0.973f, 0.443f, 0.443f, 1.0f);
	const ImVec4 kDim(0.60f, 0.63f, 0.68f, 1.0f);

	enum class Level
	{
		List,   // the page list (initial focus)
		Page,   // inside the page: rows
		Footer, // Quit / Play (Quit game / Resume)
	};
	enum class Mode
	{
		Mouse, // no highlight, no prompts
		Pad,
		Keys,
	};

	// A choice the player confirms (quit, move cards, import a card); drawn over everything, owns the input.
	struct Dialog
	{
		bool open = false;
		std::string title, text;
		std::vector<std::string> buttons; // the last one is Cancel (B)
		int focus = 0;
		void (*onChoose)(int index) = nullptr; // -1 = cancelled
	};

	// Held-direction repeat (PlayFieldPortal GamepadInputHandler, STANDARD tuning): first step at once, then after
	// 250 ms every 110 ms, tightening to 50 ms over 5 repeats; full stick tilt starts at the fast interval.
	struct Repeat
	{
		bool held = false;
		double next = 0.0;
		int steps = 0;
	};

	struct State
	{
		ObUiHost host;
		obui::IniFile ini{false};  // outbreak.ini
		obui::IniFile input{true}; // input.ini (inline comments)
		// No hold-back (developer, 2026-10-26): Start and Select reach the game on their first read. Windows keeps the
		// Back + Start combo held for 2 s (a gear ring shows the hold, ob_gs_set_hold_progress); Android has no combo,
		// its Back button opens the menu. F1 / Esc everywhere. The pad gate still masks the buttons held when the menu
		// closes.
#if defined(__ANDROID__)
		obui::MenuTrigger trigger{obui::MenuTrigger::Config{2.0, 0.0, false, false}};
#else
		obui::MenuTrigger trigger{obui::MenuTrigger::Config{2.0, 0.0, false, true}};
#endif
		obui::DiscCache discCache;
		obui::DiscReport disc;
		std::string gamePath;
		bool discChecked = false;
		bool launcher = false;   // building the launcher (false: the in-game menu)
		bool menuOpen = false;
		bool inputChanged = false;
		bool gsChanged = false;  // a setting the GS opens with changed in the launcher (the hook reopens it)
		Page page = Page::Game;
		int bindDevice = 0;      // 0 keyboard, 1 controller
		int captureRow = -1;     // bind row being captured (kBindRows index; 100+ = kStickRows)
		double captureStart = 0.0;
		std::string status;
		double statusUntil = 0.0;
		double now = 0.0;
		ObUiInput in;
		int frameResult = 0;     // set by Build: 1 play / resume, 2 quit
		std::string cardPrompt;  // a new saves folder waiting for move / copy / leave
		std::string importPrompt;// a card file waiting for "replace slot 1?"
		std::string modPrompt;   // a mod file waiting for "delete?"
		bool modsListed = false; // the Mods page's file list is current (cleared by any change and every 2 s)
		double modsListedAt = 0.0;
		bool wideKnown = false;  // Display > Aspect ratio: whether a widescreen mod is enabled (core/widescreen.h)
		bool wideOn = false;
		double wideCheckedAt = 0.0;
#if defined(__ANDROID__)
		int discFd = -1;
		bool kotlinMenu = false; // the Kotlin menu is open (android_menu.h)
#endif
		// Navigation.
		Level level = Level::List;
		Mode mode = Mode::Mouse;
		int rowFocus = 0;
		int rowCount = 0;        // focusable rows drawn last frame on the page
		int rowCounter = 0;      // rows registered this frame
		int lastRow[static_cast<int>(Page::Count)] = {};
		int subFocus = 0;        // the button inside an action row
		int footerFocus = 1;     // 0 Quit, 1 Play / Resume
		bool scrollToFocus = false;
		Repeat rep[4];           // up, down, left, right
		bool stick[4] = {};      // stick direction engaged (hysteresis)
		ObUiInput::Held prevPad, prevKeys;
		bool evUp = false, evDown = false, evLeft = false, evRight = false, evA = false, evB = false, evY = false,
			 evL1 = false, evR1 = false, evStart = false;
		Dialog dialog;
		// Look.
		ImFont* fontText = nullptr;
		ImFont* fontBold = nullptr;
		bool fontsTried = false;
		float px = 16.0f;        // body text size this frame
		ImGuiStyle baseStyle;
		bool styleReady = false;
		ImDrawList* pageDl = nullptr; // the page child's draw list (channels: 0 cards, 1 focus, 2 content)
		float cardX = 0.0f, cardW = 0.0f;
	};
	State* s = nullptr;

	// ---- in-app file / folder browser (controller and keyboard; the mouse gets Windows' own dialogs) ----
	struct Browser
	{
		bool open = false;
		bool folders = false;      // pick a folder (else a file with ext)
		std::string title, ext;    // ext: ".iso", ".ps2" (lower case)
		fs::path cwd;              // empty = the list of drives (Windows) / "/" elsewhere
		std::vector<fs::path> dirs, files;
		bool listed = false;
		void (*onPick)(const std::string&) = nullptr;
	};
	Browser g_browser;

	// ---- settings access ----
	std::string Get(const char* key, const char* def = "")
	{
		auto v = s->ini.get("", key);
		return v && !v->empty() ? *v : std::string(def);
	}
	std::string Ui(const char* key, const char* def = "")
	{
		auto v = s->ini.get("ui", key);
		return v && !v->empty() ? *v : std::string(def);
	}
	void Save(const char* what)
	{
		if (!s->host.settingsFile.empty() && !s->ini.save(s->host.settingsFile))
			std::fprintf(stderr, "[ui] could not write %s\n", s->host.settingsFile.c_str());
		s->status = std::string("Saved: ") + what;
		s->statusUntil = s->now + 2.5;
	}
	void Set(const char* key, const std::string& value, const char* what)
	{
		s->ini.set("", key, value);
		Save(what);
	}
	void SetUi(const char* key, const std::string& value, const char* what)
	{
		s->ini.set("ui", key, value);
		Save(what);
	}
	void Note(const std::string& text)
	{
		s->status = text;
		s->statusUntil = s->now + 4.0;
	}
	fs::path SettingsDir()
	{
		return fs::u8path(s->host.settingsFile).parent_path();
	}
	// A settings path as the runner reads it: relative to the settings file's folder.
	fs::path Resolve(const std::string& value)
	{
		if (value.empty())
			return {};
		fs::path p = fs::u8path(value);
		return p.is_relative() ? (SettingsDir() / p).lexically_normal() : p;
	}
	std::string Bind(int device, const char* key, const char* def)
	{
		auto v = s->input.get(device ? "gamepad" : "keyboard", key);
		return v && !v->empty() ? *v : std::string(def);
	}
	void SetBind(int device, const char* key, const std::string& value)
	{
		s->input.set(device ? "gamepad" : "keyboard", key, value);
		if (!s->host.inputFile.empty())
			s->input.save(s->host.inputFile);
		s->inputChanged = true;
		Note("Saved: binding");
	}
	fs::path CardFolder()
	{
		const char* env = std::getenv("PS2X_SAVES");
		if (env && *env)
			return fs::u8path(env);
		return Get("saves").empty() ? SettingsDir() / "memcards" : Resolve(Get("saves"));
	}

	// ---- the game disc ----
	void CheckDisc()
	{
		const fs::path p = fs::u8path(s->gamePath);
		if (s->gamePath.empty())
		{
			s->disc = obui::DiscReport{};
			s->disc.message = "Choose your game disc to play.";
		}
		else if (s->discCache.valid && s->discCache.sameFileAs(p))
			s->disc = s->discCache.report;
		else
		{
			s->disc = obui::checkDisc(p, obui::builtInExpectedElf());
			s->discCache.remember(p, s->disc);
			std::fprintf(stderr, "[ui] disc check %s: %d/3 %s\n", s->gamePath.c_str(), s->disc.stepsPassed, s->disc.message.c_str());
		}
		s->discChecked = true;
	}
	bool DiscOk() { return s->discChecked && s->disc.verdict == obui::DiscVerdict::Ok; }

	void ChooseDisc(const std::string& path, bool remember)
	{
		s->gamePath = path;
		s->discChecked = false;
		CheckDisc();
		if (remember && DiscOk() && !s->host.android)
			Set("game", path, "game disc");
	}

	// ---- friendly names ----
	std::string PadLabel(const std::string& names)
	{
		struct N { const char* raw; const char* nice; };
		static const N k[] = {{"RIGHT_FACE_DOWN", "A"}, {"RIGHT_FACE_RIGHT", "B"}, {"RIGHT_FACE_LEFT", "X"},
			{"RIGHT_FACE_UP", "Y"}, {"LEFT_FACE_UP", "D-pad up"}, {"LEFT_FACE_DOWN", "D-pad down"},
			{"LEFT_FACE_LEFT", "D-pad left"}, {"LEFT_FACE_RIGHT", "D-pad right"}, {"LEFT_TRIGGER_1", "LB"},
			{"RIGHT_TRIGGER_1", "RB"}, {"LEFT_TRIGGER_2", "LT"}, {"RIGHT_TRIGGER_2", "RT"}, {"LEFT_TRIGGER", "LT"},
			{"RIGHT_TRIGGER", "RT"}, {"LEFT_THUMB", "Left stick click"}, {"RIGHT_THUMB", "Right stick click"},
			{"MIDDLE_RIGHT", "Start"}, {"MIDDLE_LEFT", "Back"}, {"MIDDLE", "Guide"}};
		const std::string first = names.substr(0, names.find(','));
		for (const N& n : k)
			if (first == n.raw)
				return n.nice;
		return first;
	}
	std::string StickPart(const std::string& list, int index)
	{
		std::vector<std::string> parts;
		std::stringstream ss(list);
		std::string item;
		while (std::getline(ss, item, ','))
		{
			item.erase(0, item.find_first_not_of(' '));
			item.erase(item.find_last_not_of(' ') + 1);
			parts.push_back(item);
		}
		return index < static_cast<int>(parts.size()) ? parts[index] : std::string();
	}
	std::string StickSet(const std::string& list, int index, const std::string& value)
	{
		std::string parts[4];
		for (int i = 0; i < 4; i++)
			parts[i] = StickPart(list, i);
		parts[index] = value;
		return parts[0] + ", " + parts[1] + ", " + parts[2] + ", " + parts[3];
	}

	// ---- input: edges, repeat, stick hysteresis, input mode ----
	bool StepRepeat(int d, bool held, bool fast)
	{
		Repeat& r = s->rep[d];
		if (!held)
		{
			r.held = false;
			return false;
		}
		if (!r.held)
		{
			r.held = true;
			r.steps = 0;
			r.next = s->now + (fast ? 0.05 : 0.25);
			return true;
		}
		if (s->now < r.next)
			return false;
		const double base = 0.110, quick = 0.050;
		const int ramp = 5;
		const double interval = fast ? quick : base - (base - quick) * std::min(r.steps, ramp) / ramp;
		r.steps++;
		r.next = s->now + interval;
		return true;
	}

	void ReadNav()
	{
		const ObUiInput& in = s->in;
		// Stick: a direction engages past 0.5 on its dominant axis and releases below 0.3 (0.5 x 0.6), so noise at
		// the edge cannot flap it on and off.
		const float x = in.padLeftX, y = in.padLeftY;
		const float ax = std::fabs(x), ay = std::fabs(y);
		const bool want[4] = {y < 0 && ay >= ax, y > 0 && ay >= ax, x < 0 && ax > ay, x > 0 && ax > ay};
		const float mag[4] = {ay, ay, ax, ax};
		for (int d = 0; d < 4; d++)
			s->stick[d] = want[d] && (s->stick[d] ? mag[d] > 0.3f : mag[d] > 0.5f);
		const bool fast = std::max(ax, ay) > 0.9f;
		const bool padDir[4] = {in.pad.up || s->stick[0], in.pad.down || s->stick[1], in.pad.left || s->stick[2],
			in.pad.right || s->stick[3]};
		// A text box being typed in owns the keyboard: its arrows and Backspace are not the menu's.
		const ObUiInput::Held keys = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput ? ObUiInput::Held{} : in.keys2;
		const bool keyDir[4] = {keys.up, keys.down, keys.left, keys.right};
		bool ev[4];
		for (int d = 0; d < 4; d++)
			ev[d] = StepRepeat(d, padDir[d] || keyDir[d], fast && s->stick[d]);
		s->evUp = ev[0];
		s->evDown = ev[1];
		s->evLeft = ev[2];
		s->evRight = ev[3];
		auto edge = [](bool now, bool before) { return now && !before; };
		s->evA = edge(in.pad.a, s->prevPad.a) || edge(keys.a, s->prevKeys.a);
		s->evB = edge(in.pad.b, s->prevPad.b) || edge(keys.b, s->prevKeys.b);
		s->evY = edge(in.pad.y, s->prevPad.y);
		s->evL1 = edge(in.pad.l1, s->prevPad.l1) || edge(keys.l1, s->prevKeys.l1);
		s->evR1 = edge(in.pad.r1, s->prevPad.r1) || edge(keys.r1, s->prevKeys.r1);
		s->evStart = edge(in.pad.start, s->prevPad.start);
		// Input mode: the last device used. Controller or keyboard shows the focus and the prompts; the mouse hides them.
		const bool padUsed = (padDir[0] || padDir[1] || padDir[2] || padDir[3]) || edge(in.pad.a, s->prevPad.a) ||
			edge(in.pad.b, s->prevPad.b) || edge(in.pad.y, s->prevPad.y) || edge(in.pad.l1, s->prevPad.l1) ||
			edge(in.pad.r1, s->prevPad.r1) || edge(in.pad.start, s->prevPad.start);
		const bool keysUsed = (keyDir[0] || keyDir[1] || keyDir[2] || keyDir[3]) || edge(keys.a, s->prevKeys.a) ||
			edge(keys.b, s->prevKeys.b) || edge(keys.l1, s->prevKeys.l1) || edge(keys.r1, s->prevKeys.r1);
		if (padUsed)
			s->mode = Mode::Pad;
		else if (keysUsed)
			s->mode = Mode::Keys;
		else if (in.mouseMoved)
			s->mode = Mode::Mouse;
		s->prevPad = in.pad;
		s->prevKeys = keys;
	}
	bool Steering() { return s->mode != Mode::Mouse; }
	void ClearNav() { s->evUp = s->evDown = s->evLeft = s->evRight = s->evA = s->evB = s->evY = s->evL1 = s->evR1 = s->evStart = false; }

	// ---- rows (the step-in model's unit) ----
	struct Row
	{
		int index = -1;
		bool focused = false;
		ImVec2 min;
	};
	Row BeginRow(bool focusable = true)
	{
		Row r;
		if (focusable)
		{
			r.index = s->rowCounter++;
			r.focused = s->level == Level::Page && Steering() && !g_browser.open ? r.index == s->rowFocus
			                                                                      : (g_browser.open && Steering() && r.index == s->rowFocus);
		}
		r.min = ImGui::GetCursorScreenPos();
		ImGui::PushID(r.index >= 0 ? r.index : 100000 + static_cast<int>(r.min.y));
		ImGui::BeginGroup();
		return r;
	}
	void EndRow(const Row& r)
	{
		ImGui::EndGroup();
		ImGui::PopID();
		if (!r.focused)
			return;
		const ImVec2 max = ImGui::GetItemRectMax();
		const float pad = 0.35f * s->px;
		const ImVec2 a(s->cardX + pad * 0.6f, r.min.y - pad * 0.5f), b(s->cardX + s->cardW - pad * 0.6f, max.y + pad * 0.5f);
		if (s->pageDl)
		{
			s->pageDl->ChannelsSetCurrent(1);
			s->pageDl->AddRectFilled(a, b, kFocusBg, 0.45f * s->px);
			s->pageDl->AddRect(a, b, kFocusEdge, 0.45f * s->px, std::max(1.0f, s->px / 12.0f));
			s->pageDl->ChannelsSetCurrent(2);
		}
		if (s->scrollToFocus)
		{
			ImGui::ScrollToRectEx(ImGui::GetCurrentWindow(), ImRect(a, b), ImGuiScrollFlags_KeepVisibleEdgeY);
			s->scrollToFocus = false;
		}
	}
	// The focused row's left/right this frame: -1, 0 or 1 (consumed).
	int RowStep(const Row& r)
	{
		if (!r.focused)
			return 0;
		const int d = s->evLeft ? -1 : s->evRight ? 1 : 0;
		s->evLeft = s->evRight = false;
		return d;
	}
	bool RowA(const Row& r)
	{
		if (!r.focused || !s->evA)
			return false;
		s->evA = false;
		return true;
	}

	// ---- cards and widgets ----
	float LabelWidth() { return std::max(9.0f * s->px, s->cardW * 0.36f); }
	float ControlWidth() { return std::max(6.0f * s->px, s->cardW - LabelWidth() - 1.6f * s->px); }

	void Hint(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kDim);
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

	ImVec2 g_cardStart;
	void BeginCard(const char* label)
	{
		ImGui::Dummy(ImVec2(0, 0.3f * s->px));
		g_cardStart = ImGui::GetCursorScreenPos();
		s->cardX = g_cardStart.x;
		s->cardW = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize * 0.5f; // clear of the scrollbar
		ImGui::Dummy(ImVec2(0, 0.55f * s->px));
		ImGui::Indent(0.9f * s->px);
		if (label && *label)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kDim);
			ImGui::PushFont(s->fontBold, s->px * 0.85f);
			ImGui::TextUnformatted(label);
			ImGui::PopFont();
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0, 0.15f * s->px));
		}
	}
	void EndCard()
	{
		ImGui::Dummy(ImVec2(0, 0.45f * s->px));
		ImGui::Unindent(0.9f * s->px);
		const ImVec2 end(g_cardStart.x + s->cardW, ImGui::GetCursorScreenPos().y);
		if (s->pageDl)
		{
			s->pageDl->ChannelsSetCurrent(0);
			s->pageDl->AddRectFilled(g_cardStart, end, kCardBg, 0.7f * s->px);
			s->pageDl->ChannelsSetCurrent(2);
		}
	}

	// A label on the left, then the control at the label column.
	void RowLabel(const char* label)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		ImGui::SetCursorScreenPos(ImVec2(s->cardX + LabelWidth(), ImGui::GetCursorScreenPos().y));
	}

	// A non-focusable line: label and value (optional colour).
	void Info(const char* label, const std::string& value, const ImVec4* color = nullptr)
	{
		Row r = BeginRow(false);
		if (label)
			RowLabel(label);
		if (color)
			ImGui::PushStyleColor(ImGuiCol_Text, *color);
		ImGui::PushTextWrapPos(s->cardX + s->cardW - 0.9f * s->px - ImGui::GetWindowPos().x + ImGui::GetScrollX());
		ImGui::TextWrapped("%s", value.c_str());
		ImGui::PopTextWrapPos();
		if (color)
			ImGui::PopStyleColor();
		EndRow(r);
	}

	// A status line with an icon (the disc check's steps): icon and text centred on the line by their ink.
	void InfoIcon(unsigned icon, const char* text, const ImVec4& color)
	{
		Row r = BeginRow(false);
		ImGui::Dummy(ImVec2(IconTextWidth(icon, text), ImGui::GetTextLineHeight()));
		const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		IconText(ImGui::GetWindowDrawList(), a.x, (a.y + b.y) * 0.5f, icon, text, ImGui::GetColorU32(color));
		EndRow(r);
	}

	// Segmented choice (up to 4 options) or a drop-down (more); left/right cycle. True when value changed.
	bool Choice(const char* label, int& value, const char* const* options, int count, bool enabled = true)
	{
		Row r = BeginRow(enabled);
		RowLabel(label);
		const int was = value;
		if (const int d = RowStep(r))
			value = std::clamp(value + d, 0, count - 1);
		ImGui::BeginDisabled(!enabled);
		if (count <= 4)
		{
			const float gap = 0.25f * s->px;
			const float w = (ControlWidth() - gap * (count - 1)) / count;
			for (int i = 0; i < count; i++)
			{
				if (i)
					ImGui::SameLine(0, gap);
				const bool sel = value == i;
				if (sel)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
				}
				if (ImGui::Button(options[i], ImVec2(w, 0)))
					value = i;
				if (sel)
					ImGui::PopStyleColor(2);
			}
		}
		else
		{
			ImGui::SetNextItemWidth(ControlWidth());
			if (ImGui::BeginCombo("##c", options[std::clamp(value, 0, count - 1)]))
			{
				for (int i = 0; i < count; i++)
					if (ImGui::Selectable(options[i], i == value))
						value = i;
				ImGui::EndCombo();
			}
		}
		ImGui::EndDisabled();
		if (RowA(r) && count > 4)
			value = (value + 1) % count;
		EndRow(r);
		return enabled && value != was;
	}

	bool Toggle(const char* label, bool& v, bool enabled = true)
	{
		Row r = BeginRow(enabled);
		RowLabel(label);
		const bool was = v;
		if (const int d = RowStep(r))
			v = d > 0;
		if (RowA(r))
			v = !v;
		ImGui::BeginDisabled(!enabled);
		const char* const names[] = {"Off", "On"};
		const float gap = 0.25f * s->px;
		const float w = std::min(ControlWidth(), 12.0f * s->px);
		for (int i = 0; i < 2; i++)
		{
			if (i)
				ImGui::SameLine(0, gap);
			const bool sel = (v ? 1 : 0) == i;
			if (sel)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
			}
			if (ImGui::Button(names[i], ImVec2((w - gap) / 2, 0)))
				v = i == 1;
			if (sel)
				ImGui::PopStyleColor(2);
		}
		ImGui::EndDisabled();
		EndRow(r);
		return enabled && v != was;
	}

	bool Slider(const char* label, int& v, int lo, int hi, int step, const char* fmt)
	{
		Row r = BeginRow();
		RowLabel(label);
		const int was = v;
		if (const int d = RowStep(r))
			v = std::clamp(v + d * step, lo, hi);
		ImGui::SetNextItemWidth(ControlWidth());
		ImGui::SliderInt("##s", &v, lo, hi, fmt);
		EndRow(r);
		return v != was;
	}

	// A row of buttons; left/right move between them, A presses. Returns the index pressed (mouse or A), -1 none.
	int Actions(const char* label, const char* const* buttons, int count, bool enabled = true)
	{
		Row r = BeginRow(enabled);
		if (label)
			RowLabel(label);
		if (r.focused)
			s->subFocus = std::clamp(s->subFocus + RowStep(r), 0, count - 1);
		int pressed = -1;
		ImGui::BeginDisabled(!enabled);
		for (int i = 0; i < count; i++)
		{
			if (i)
				ImGui::SameLine(0, 0.4f * s->px);
			const bool hot = r.focused && s->subFocus == i;
			if (hot)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
			}
			if (ImGui::Button(buttons[i]))
				pressed = i;
			if (hot)
				ImGui::PopStyleColor(2);
		}
		ImGui::EndDisabled();
		if (RowA(r))
			pressed = s->subFocus;
		EndRow(r);
		return enabled ? pressed : -1;
	}

	// A full-width selectable line (browser entries, driver list). True when chosen (click or A).
	// A list entry; icon != 0: drawn before the text, both centred on the row (IconText).
	bool Pick(const std::string& text, bool selected = false, bool enabled = true, unsigned icon = 0)
	{
		Row r = BeginRow(enabled);
		ImGui::BeginDisabled(!enabled);
		const bool clicked = ImGui::Selectable(icon ? ("##" + text).c_str() : text.c_str(), selected, 0,
			ImVec2(s->cardW - 1.8f * s->px, icon ? ImGui::GetTextLineHeight() : 0.0f));
		if (icon)
		{
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			IconText(ImGui::GetWindowDrawList(), a.x, (a.y + b.y) * 0.5f, icon, text.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
		}
		ImGui::EndDisabled();
		const bool a = RowA(r);
		EndRow(r);
		return enabled && (clicked || a);
	}

	void LaunchOnlyNote()
	{
		if (!s->launcher)
			Hint("Change these in the launcher. They apply the next time the game starts.");
	}

	// ---- the in-app browser ----
	void ListBrowser()
	{
		Browser& b = g_browser;
		b.dirs.clear();
		b.files.clear();
		b.listed = true;
		s->rowFocus = 0;
		s->scrollToFocus = true;
		std::error_code ec;
		if (b.cwd.empty())
		{
#if defined(_WIN32)
			for (char d = 'A'; d <= 'Z'; d++)
			{
				const fs::path root = std::string(1, d) + ":/";
				if (fs::exists(root, ec))
					b.dirs.push_back(root);
			}
			return;
#else
			b.cwd = "/";
#endif
		}
		for (fs::directory_iterator it(b.cwd, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
			 it.increment(ec))
		{
			std::error_code e2;
			if (it->is_directory(e2))
				b.dirs.push_back(it->path());
			else if (!b.folders && obui::toLower(U8(it->path().extension())) == b.ext)
				b.files.push_back(it->path());
			if (b.dirs.size() + b.files.size() > 4000)
				break;
		}
		auto byName = [](const fs::path& a, const fs::path& c) {
			return obui::toLower(U8(a.filename())) < obui::toLower(U8(c.filename()));
		};
		std::sort(b.dirs.begin(), b.dirs.end(), byName);
		std::sort(b.files.begin(), b.files.end(), byName);
	}

	void OpenBrowser(const char* title, bool folders, const char* ext, fs::path start, void (*onPick)(const std::string&))
	{
		Browser& b = g_browser;
		b = Browser{};
		b.open = true;
		b.title = title;
		b.folders = folders;
		b.ext = ext;
		b.onPick = onPick;
		std::error_code ec;
		if (start.empty())
			start = SettingsDir();
		while (!start.empty() && !fs::is_directory(start, ec))
			start = start.has_parent_path() && start.parent_path() != start ? start.parent_path() : fs::path();
		b.cwd = start;
	}

	void BrowserUp()
	{
		Browser& b = g_browser;
		if (b.cwd.empty())
		{
			b.open = false; // up from the drive list = cancel
			s->scrollToFocus = true;
			return;
		}
#if defined(_WIN32)
		b.cwd = (b.cwd == b.cwd.root_path() || !b.cwd.has_relative_path()) ? fs::path() : b.cwd.parent_path();
#else
		if (b.cwd == b.cwd.root_path())
		{
			b.open = false;
			return;
		}
		b.cwd = b.cwd.parent_path();
#endif
		b.listed = false;
	}

	void BrowserPick(const fs::path& p)
	{
		void (*cb)(const std::string&) = g_browser.onPick;
		g_browser.open = false;
		s->rowFocus = s->lastRow[static_cast<int>(s->page)];
		s->scrollToFocus = true;
		if (cb)
			cb(U8(p));
	}

	// Browse: Windows' own dialog with the mouse; the in-app browser with a controller or the keyboard.
	void Browse(const char* title, bool folders, const char* ext, const char* filterName, const char* pattern,
		const fs::path& start, void (*onPick)(const std::string&))
	{
#if defined(_WIN32)
		if (!Steering())
		{
			const std::string p = folders ? obui::pickFolder(title) : obui::pickFile(title, filterName, pattern, U8(start));
			if (!p.empty())
				onPick(p);
			return;
		}
#endif
		s->lastRow[static_cast<int>(s->page)] = s->rowFocus;
		OpenBrowser(title, folders, ext, start, onPick);
		s->rowFocus = 0;
	}

	void DrawBrowser()
	{
		Browser& b = g_browser;
		if (!b.listed)
			ListBrowser();
		BeginCard(b.title.c_str());
		Info(nullptr, b.cwd.empty() ? std::string("This PC") : U8(b.cwd));
		const char* const withUse[] = {"Use this folder", "Cancel"};
		const char* const cancelOnly[] = {"Cancel"};
		const bool canUse = b.folders && !b.cwd.empty();
		const int a = canUse ? Actions(nullptr, withUse, 2) : Actions(nullptr, cancelOnly, 1);
		if ((canUse && a == 0))
			BrowserPick(b.cwd);
		else if (a >= 0)
		{
			b.open = false;
			s->rowFocus = s->lastRow[static_cast<int>(s->page)];
			s->scrollToFocus = true;
		}
		EndCard();
		if (!b.open)
			return;
		BeginCard(nullptr);
		fs::path enter;
		bool up = false;
		if (Pick(".. (up one level)"))
			up = true;
		for (const fs::path& d : b.dirs)
		{
			const std::string name = b.cwd.empty() ? U8(d) : U8(d.filename());
			if (Pick(name, false, true, 0xE8B7))
				enter = d;
		}
		for (const fs::path& f : b.files)
			if (Pick(U8(f.filename()), false, true, 0xE7C3))
				enter = f;
		if (b.dirs.empty() && b.files.empty())
			Hint(b.folders ? "No folders here." : ("No " + b.ext + " files here.").c_str());
		EndCard();
		if (s->evB)
		{
			s->evB = false;
			up = true;
		}
		if (canUse && s->evY)
		{
			s->evY = false;
			BrowserPick(b.cwd);
			return;
		}
		if (!enter.empty())
		{
			std::error_code ec;
			if (fs::is_directory(enter, ec))
			{
				b.cwd = enter;
				b.listed = false;
			}
			else
				BrowserPick(enter);
		}
		else if (up && b.open)
			BrowserUp();
	}

	// ---- dialogs ----
	void OpenDialog(const char* title, const std::string& text, std::vector<std::string> buttons, void (*onChoose)(int),
		int focus = 0)
	{
		s->dialog = Dialog{};
		s->dialog.open = true;
		s->dialog.title = title;
		s->dialog.text = text;
		s->dialog.buttons = std::move(buttons);
		s->dialog.focus = std::clamp(focus, 0, static_cast<int>(s->dialog.buttons.size()) - 1);
		s->dialog.onChoose = onChoose;
	}

	void DrawDialog()
	{
		Dialog& d = s->dialog;
		const int n = static_cast<int>(d.buttons.size());
		int chosen = -2;
		if (s->evLeft || s->evUp)
			d.focus = std::max(0, d.focus - 1);
		if (s->evRight || s->evDown)
			d.focus = std::min(n - 1, d.focus + 1);
		if (s->evA)
			chosen = d.focus;
		if (s->evB)
			chosen = -1;
		ClearNav();
		const ImVec2 full = ImGui::GetIO().DisplaySize;
		ImGui::GetForegroundDrawList(); // keep the order: the dialog window is created last and focused
		const float w = std::min(full.x - 2.0f * s->px, 34.0f * s->px);
		ImGui::SetNextWindowPos(ImVec2(full.x * 0.5f, full.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(w, 0));
		ImGui::SetNextWindowFocus();
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kCardBg));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.2f * s->px, 1.0f * s->px));
		ImGui::Begin("##dialog", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::PushFont(s->fontBold, s->px * 1.1f);
		ImGui::TextUnformatted(d.title.c_str());
		ImGui::PopFont();
		if (!d.text.empty())
		{
			ImGui::Dummy(ImVec2(0, 0.3f * s->px));
			ImGui::PushTextWrapPos(w - 1.2f * s->px);
			ImGui::TextWrapped("%s", d.text.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::Dummy(ImVec2(0, 0.6f * s->px));
		for (int i = 0; i < n; i++)
		{
			if (i)
				ImGui::SameLine(0, 0.5f * s->px);
			const bool hot = Steering() && d.focus == i;
			if (hot)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
			}
			if (ImGui::Button(d.buttons[i].c_str()))
				chosen = i;
			if (hot)
				ImGui::PopStyleColor(2);
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		if (chosen != -2)
		{
			void (*cb)(int) = d.onChoose;
			const int pick = chosen == n - 1 ? -1 : chosen; // the last button is Cancel
			d.open = false;
			if (cb)
				cb(pick);
		}
	}

	// ---- pages ----
	void PageGame()
	{
		BeginCard("GAME DISC  -  REQUIRED");
		Info(nullptr, s->gamePath.empty() ? std::string("No disc chosen") : s->gamePath);
		if (s->launcher)
		{
			const char* const btn[] = {"Browse...", "Check again"};
			const int a = Actions(nullptr, btn, 2);
			if (a == 0)
			{
#if defined(__ANDROID__)
				obui::android::startPick(obui::android::PickKind::Disc);
#else
				Browse("Choose your game disc (.iso)", false, ".iso", "Disc images (*.iso)", "*.iso",
					s->gamePath.empty() ? fs::path() : fs::u8path(s->gamePath).parent_path(),
					[](const std::string& p) { ChooseDisc(p, true); });
#endif
			}
			else if (a == 1)
			{
				s->discCache.valid = false;
				CheckDisc();
			}
		}
		const char* steps[3] = {"File opens and reads as a disc image", "Disc boots SLPM_654.28 (SYSTEM.CNF)",
			"Game code matches this build"};
		for (int i = 0; i < 3; i++)
		{
			const bool passed = s->discChecked && s->disc.stepsPassed > i;
			const bool failed = s->discChecked && !s->gamePath.empty() && s->disc.stepsPassed == i && !DiscOk();
			const ImVec4 c = passed ? kSuccess : failed ? kDanger : kDim;
			InfoIcon(passed ? 0xE73E : failed ? 0xE711 : 0xE738, steps[i], c);
		}
		if (s->discChecked)
			Info(nullptr, s->disc.message, DiscOk() ? &kSuccess : &kWarn);
		EndCard();

		BeginCard("TEXTURE PACK  -  OPTIONAL");
		const std::string tex = Get("textures");
		Info(nullptr, tex.empty() ? std::string("None: the game's original textures") : tex);
		if (s->launcher)
		{
			const char* const btn[] = {s->host.android ? "Import .zip" : "Browse...", "Clear"};
			const int a = Actions(nullptr, btn, 2);
			if (a == 0)
			{
#if defined(__ANDROID__)
				obui::android::startPick(obui::android::PickKind::TexturesZip);
#else
				Browse("Choose the texture pack folder (the one that holds SLPM-65428)", true, "", nullptr, nullptr,
					Resolve(tex), [](const std::string& p) {
						Set("textures", p, "texture pack");
						s->gsChanged = true;
						std::error_code ec;
						if (!fs::exists(fs::u8path(p) / s->host.gameSerial, ec) &&
							!fs::exists(fs::u8path(p) / (s->host.gameSerial + ".zip"), ec))
							Note("No " + s->host.gameSerial + " pack in that folder. The game plays with its own textures.");
					});
#endif
			}
			else if (a == 1 && !tex.empty())
			{
				Set("textures", "", "texture pack");
				s->gsChanged = true;
			}
		}
		EndCard();

		BeginCard("MODS  -  OPTIONAL");
		const std::string mods = Get("mods");
		Info(nullptr, mods.empty() ? std::string("None") : mods);
		Hint("Choose the folder and turn mods on or off on the Mods page.");
		EndCard();

		if (s->launcher)
		{
			BeginCard("START-UP");
			bool show = Ui("launcher", "on") != "off";
			if (Toggle("Show this screen at start-up", show))
				SetUi("launcher", show ? "on" : "off", "start-up screen");
			#if defined(__ANDROID__)
		Hint("Off boots straight into the game when the disc checks out. The Back button still opens the menu.");
#else
		Hint("Off boots straight into the game when the disc checks out. F1, Esc or holding Back + Start for 2 s still opens the menu.");
#endif
			EndCard();
		}
	}

	void PageControls()
	{
		BeginCard("BINDINGS");
		const char* const devs[] = {"Keyboard", "Controller"};
		Choice("Bindings for", s->bindDevice, devs, 2);
		Hint("Classic camera layout (PS2 buttons). The Modern camera gets its own layout when it arrives.");
		EndCard();

		const int dev = s->bindDevice;
		BeginCard(dev ? "CONTROLLER BUTTONS" : "KEYBOARD KEYS");
		auto bindRow = [&](int id, const char* label, const std::string& shown) {
			const bool capturing = s->captureRow == id;
			const std::string text = capturing ? (dev ? "Press a button...  (6 s to cancel)" : "Press a key...  (Esc cancels)") : shown;
			const char* const btn[] = {text.c_str()};
			if (Actions(label, btn, 1) == 0 && s->captureRow < 0)
			{
				s->captureRow = id;
				s->captureStart = s->now;
			}
		};
		for (int i = 0; i < static_cast<int>(sizeof(kBindRows) / sizeof(kBindRows[0])); i++)
		{
			const BindRow& r = kBindRows[i];
			const std::string v = Bind(dev, r.key, dev ? r.gamepad : r.keyboard);
			bindRow(i, r.label, dev ? PadLabel(v) : v.substr(0, v.find(',')));
		}
		if (dev == 0)
			for (int i = 0; i < static_cast<int>(sizeof(kStickRows) / sizeof(kStickRows[0])); i++)
			{
				const StickRow& r = kStickRows[i];
				const std::string list = Bind(0, r.key, std::string(r.key) == "stick_left" ? "W, S, A, D" : "T, G, F, H");
				const std::string part = StickPart(list, r.index);
				bindRow(100 + i, r.label, part.empty() ? r.def : part);
			}
		EndCard();

		// Capture: the first key / button pressed after the one that started it.
		if (s->captureRow >= 0 && s->now - s->captureStart > 6.0)
		{
			s->captureRow = -1; // a controller has no free button to cancel with: time out instead
			Note("Binding not changed");
		}
		if (s->captureRow >= 0 && s->now - s->captureStart > 0.25)
		{
			const std::string got = dev ? s->in.padPressedName : s->in.keyPressedName;
			if (!dev && got == "ESCAPE")
				s->captureRow = -1;
			else if (!got.empty())
			{
				bool reserved = false;
				for (const std::string& k : obui::reservedKeyNames())
					reserved |= (!dev && got == k);
				if (reserved || (dev && got == "MIDDLE"))
					Note(got + " opens the menu. Pick another.");
				else if (s->captureRow >= 100)
				{
					const StickRow& r = kStickRows[s->captureRow - 100];
					const std::string list = Bind(0, r.key, std::string(r.key) == "stick_left" ? "W, S, A, D" : "T, G, F, H");
					SetBind(0, r.key, StickSet(list, r.index, got));
					s->captureRow = -1;
				}
				else
				{
					const BindRow& r = kBindRows[s->captureRow];
					std::string v = got;
					if (dev && (got == "LEFT_TRIGGER_2" || got == "RIGHT_TRIGGER_2"))
						v = got + ", " + got.substr(0, got.size() - 2); // the analog trigger too
					SetBind(dev, r.key, v);
					s->captureRow = -1;
				}
			}
		}

		BeginCard("MORE");
		if (dev == 1)
		{
			int dz = static_cast<int>(std::lround(std::atof(Bind(1, "deadzone_left", "0.239540").c_str()) * 100.0));
			if (Slider("Stick dead zone", dz, 0, 50, 1, "%d%%"))
			{
				char v[32];
				std::snprintf(v, sizeof(v), "%.6f", dz / 100.0);
				s->input.set("gamepad", "deadzone_left", v);
				s->input.set("gamepad", "deadzone_right", v);
				if (!s->host.inputFile.empty())
					s->input.save(s->host.inputFile);
				s->inputChanged = true;
			}
		}
		const char* const reset[] = {"Reset to defaults"};
		if (Actions(nullptr, reset, 1) == 0)
		{
			for (const BindRow& r : kBindRows)
				s->input.remove(dev ? "gamepad" : "keyboard", r.key);
			if (!dev)
			{
				s->input.remove("keyboard", "stick_left");
				s->input.remove("keyboard", "stick_right");
			}
			if (!s->host.inputFile.empty())
				s->input.save(s->host.inputFile);
			s->inputChanged = true;
			Note("Defaults restored");
		}
		Hint("F1 and Esc are kept for the menu.");
		EndCard();
	}

	void PageCamera()
	{
		BeginCard("CAMERA");
		int cam = 0;
		const char* const cams[] = {"Classic", "Modern (coming later)"};
		Choice("Camera", cam, cams, 2, false);
		Hint("Classic: the original fixed angles, as on PS2. Modern: over the shoulder, the right stick turns the camera, free aim. Its options appear when it arrives.");
		EndCard();
	}

	// The saved window size and mode, on the game window: at Play, or at start-up when the launcher is skipped.
	void ApplyGameWindow()
	{
		int w = 0, h = 0;
		if (s->host.setWindowSize && obui::parseWindowSize(Get("window"), w, h))
			s->host.setWindowSize(w, h);
		const std::string m = Ui("window_mode", "windowed");
		if (s->host.setWindowMode && m != "windowed")
			s->host.setWindowMode(m == "borderless" ? 1 : 2);
	}

	void PageDisplay()
	{
		if (!s->host.android)
		{
			BeginCard("WINDOW");
			const std::string mode = Ui("window_mode", "windowed");
			int m = mode == "borderless" ? 1 : mode == "fullscreen" ? 2 : 0;
			const char* const modes[] = {"Windowed", "Borderless", "Fullscreen"};
			if (Choice("Window mode", m, modes, 3))
			{
				SetUi("window_mode", m == 1 ? "borderless" : m == 2 ? "fullscreen" : "windowed", "window mode");
				// The launcher keeps its own window: these apply to the game window, at Play (ApplyGameWindow).
				if (s->host.setWindowMode && !s->launcher)
					s->host.setWindowMode(m);
			}
			const std::string win = Get("window");
			int cur = 2;
			for (int i = 0; i < 5; i++)
				if (win == std::to_string(kWindowSizes[i].w) + "x" + std::to_string(kWindowSizes[i].h))
					cur = i;
			const char* sizes[5];
			for (int i = 0; i < 5; i++)
				sizes[i] = kWindowSizes[i].label;
			if (Choice(m == 1 ? "Output resolution (desktop)" : "Output resolution", cur, sizes, 5, m != 1))
			{
				Set("window", std::to_string(kWindowSizes[cur].w) + "x" + std::to_string(kWindowSizes[cur].h), "resolution");
				if (s->host.setWindowSize && !s->launcher)
					s->host.setWindowSize(kWindowSizes[cur].w, kWindowSizes[cur].h);
			}
			if (s->launcher)
				Hint("These apply to the game window when the game starts.");
			EndCard();
		}

		BeginCard("PICTURE");
		const int up = std::atoi(Get("upscale", s->host.android ? "3" : "1").c_str());
		int ui = 0;
		for (int i = 0; i < 5; i++)
			if (kUpscales[i] == up)
				ui = i;
		if (Choice("Render resolution", ui, kUpscaleNames, 5, s->launcher))
		{
			Set("upscale", std::to_string(kUpscales[ui]), "render resolution");
			s->gsChanged = true;
		}
		// Aspect ratio = whether a widescreen mod is enabled in <mods>/<serial>/ (core/widescreen.h: the player's own
		// widescreen .pnach or the menu's ob-widescreen.pnach; patch 0050 reads them at start-up). Checked every second
		// at most, so a file added by hand shows up.
		if (!s->wideKnown || s->now - s->wideCheckedAt > 1.0)
		{
			const std::string m = Get("mods");
			s->wideOn = !m.empty() && obui::widescreenEnabled(Resolve(m) / s->host.gameSerial);
			s->wideKnown = true;
			s->wideCheckedAt = s->now;
		}
		int wide = s->wideOn ? 1 : 0;
		const char* const aspects[] = {"4:3", "16:9"};
		if (Choice("Aspect ratio", wide, aspects, 2, s->launcher))
		{
			// A mods folder is made beside the settings file if unset. 16:9 enables a disabled widescreen mod (the
			// player's own first) or writes ob-widescreen.pnach; 4:3 renames every enabled one to .pnach.disabled.
			std::string mods = Get("mods");
			if (mods.empty())
			{
				mods = "mods";
				Set("mods", mods, "mods folder");
			}
			std::string err;
			if (obui::setWidescreen(Resolve(mods) / s->host.gameSerial, wide != 0, &err))
				SetUi("widescreen", wide ? "on" : "off", "aspect ratio");
			else
				Note(err);
			s->wideKnown = false;
			s->modsListed = false;
			s->gsChanged = true;
		}
		int sharp = Get("scaling", s->host.android ? "smooth" : "sharp") == "smooth" ? 0 : 1;
		const char* const scalings[] = {"Smooth", "Sharp"};
		if (Choice("Pixel scaling", sharp, scalings, 2, s->launcher))
		{
			Set("scaling", sharp ? "sharp" : "smooth", "pixel scaling");
			s->gsChanged = true;
		}
		LaunchOnlyNote();
		Hint("Render resolution: how sharp the 3D is drawn (Vulkan renderer only).");
		EndCard();

		BeginCard("READABILITY");
		const std::string tsv = Ui("text_size", "normal");
		int tsi = tsv == "larger" ? 2 : tsv == "large" ? 1 : 0;
		const char* const tsNames[] = {"Normal", "Large", "Larger"};
		if (Choice("Text size", tsi, tsNames, 3))
		{
			SetUi("text_size", tsi == 2 ? "larger" : tsi == 1 ? "large" : "normal", "text size");
			s->scrollToFocus = true;
		}
		EndCard();

		BeginCard("PRESENTATION");
		const std::string vs = Ui("vsync", "on");
		int v = vs == "strict" ? 1 : vs == "off" ? 2 : 0;
		const char* const vsNames[] = {"On", "Strict", "Off"};
		if (Choice("VSync", v, vsNames, 3))
		{
			SetUi("vsync", v == 1 ? "strict" : v == 2 ? "off" : "on", "vsync");
			ob_gs_set_vsync(v == 1 ? 2 : v == 2 ? 0 : 1);
		}
		bool fps = Ui("show_fps", "off") == "on";
		if (Toggle("Show FPS", fps))
		{
			SetUi("show_fps", fps ? "on" : "off", "FPS readout");
			ob_gs_set_fps_overlay(fps);
		}
		Hint("VSync On: no tearing, little delay. Strict: classic vsync, one frame more delay. Off: may tear.");
		EndCard();
	}

	void PageCards()
	{
		const char* env = std::getenv("PS2X_SAVES");
		const fs::path folder = CardFolder();
		BeginCard("SAVE FOLDER");
		Info(nullptr, U8(folder));
		if (env && *env)
			Hint("This run's cards come from PS2X_SAVES (a scripted run); the setting is used otherwise.");
		if (s->launcher && !s->host.android)
		{
			const char* const btn[] = {"Browse...", "Use the default"};
			const int a = Actions(nullptr, btn, 2);
			if (a == 0)
				Browse("Choose the folder for your memory cards", true, "", nullptr, nullptr, folder, [](const std::string& p) {
					if (fs::u8path(p) == CardFolder())
						return;
					s->cardPrompt = p;
					OpenDialog("Move your cards?", "New save folder:\n" + p + "\n\nWhat about the cards in the old one?",
						{"Move them", "Copy them", "Leave them", "Cancel"}, [](int i) {
							if (i < 0)
							{
								s->cardPrompt.clear();
								return;
							}
							const obui::CardMove how = i == 0 ? obui::CardMove::Move : i == 1 ? obui::CardMove::Copy : obui::CardMove::Leave;
							std::string err;
							if (obui::transferCards(CardFolder(), fs::u8path(s->cardPrompt), how, &err))
							{
								Set("saves", s->cardPrompt, "save folder");
								Note(i == 0 ? "Cards moved" : i == 1 ? "Cards copied" : "Save folder changed");
							}
							else
								Note("Not changed: " + err);
							s->cardPrompt.clear();
						});
				});
			else if (a == 1)
				Set("saves", "", "save folder");
		}
		LaunchOnlyNote();
		Hint("A new card is created here. The game asks to format it the first time you save.");
		EndCard();

		BeginCard("CARDS");
		for (int slot = 0; slot < 2; slot++)
		{
			const obui::CardInfo c = obui::readCard(folder / obui::kSlotCardNames[slot]);
			std::string what = !c.present ? (slot == 0 ? "None yet (made at start-up)" : "None")
			                   : !c.formatted ? "Blank (the game formats it when you first save)"
			                                  : std::to_string(c.saves.size()) + " save(s)";
			bool file1 = false;
			for (const std::string& n : c.saves)
				file1 |= n == "BISLPM-65428NETBIO";
			if (file1)
				what += ", holds File #1 data";
			Info(slot == 0 ? "Slot 1 (Mcd001.ps2)" : "Slot 2 (Mcd002.ps2)", what);
		}
		if (s->launcher)
		{
			const char* const btnWin[] = {"Import from PCSX2...", "Back up now"};
			const char* const btnAnd[] = {"Back up now"};
			const bool win = !s->host.android;
			const int a = win ? Actions(nullptr, btnWin, 2) : Actions(nullptr, btnAnd, 1);
			if (win && a == 0)
				Browse("Choose a PCSX2 memory card (.ps2)", false, ".ps2", "PCSX2 cards (*.ps2)", "*.ps2", fs::path(),
					[](const std::string& p) {
						s->importPrompt = p;
						OpenDialog("Replace slot 1?", "Copy\n" + p + "\ninto slot 1? The card now in slot 1 is backed up first.",
							{"Replace slot 1", "Cancel"}, [](int i) {
								if (i == 0)
								{
									std::string err;
									const fs::path folder = CardFolder();
									obui::backupCard(folder / obui::kSlotCardNames[0], 5, &err);
									Note(obui::importCard(fs::u8path(s->importPrompt), folder, &err) ? "Card imported" : "Import failed: " + err);
								}
								s->importPrompt.clear();
							});
					});
			else if (a == (win ? 1 : 0))
			{
				std::string err;
				Note(obui::backupCard(folder / obui::kSlotCardNames[0], 5, &err) ? "Backed up slot 1 (the newest 5 are kept)"
				                                                                  : "Backup failed: " + err);
			}
			bool autoBackup = Ui("card_backup", "on") == "on";
			if (Toggle("Back up slot 1 at each launch", autoBackup))
				SetUi("card_backup", autoBackup ? "on" : "off", "card backup");
		}
		EndCard();
	}

	void PageAudio()
	{
		BeginCard("SOUND");
		int vol = std::atoi(Ui("volume", "100").c_str());
		if (Slider("Master volume", vol, 0, 100, 5, "%d%%"))
		{
			SetUi("volume", std::to_string(vol), "volume");
			if (s->host.setVolume)
				s->host.setVolume(vol / 100.0f);
		}
		Hint("The sound device is the system's default.");
		EndCard();
	}

	// A one-line text box: click it (or A on its row) and type; Enter saves. While it has the keyboard the menu's own
	// keys (Backspace = back, arrows) are the box's (ReadNav). True when Enter saved; value is then the new text.
	bool TextField(const char* label, std::string& value, bool enabled = true, size_t maxLen = 40)
	{
		Row r = BeginRow(enabled);
		RowLabel(label);
		char buf[64] = {};
		std::snprintf(buf, sizeof(buf), "%s", value.c_str());
		ImGui::BeginDisabled(!enabled);
		if (RowA(r))
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(ControlWidth());
		const bool saved = ImGui::InputText("##t", buf, std::min(sizeof(buf), maxLen + 1), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::EndDisabled();
		EndRow(r);
		if (!enabled || !saved)
			return false;
		value = buf;
		return true;
	}

	// ---- Network (docs/plans/2026-10-28-local-play.md): online play off, local play (nearby games, hosting) or a
	// fan server. The saved choices become the game's settings at Play (ApplyNetwork). Windows only.
#if !defined(__ANDROID__)
	// The built-in host lives as long as the runner (it keeps running during the game); the search only while the
	// Network page shows local play in the launcher.
	void TryPlay(); // below, with the footer
	std::unique_ptr<ob::server::LocalHost> g_host;
	ob::server::BeaconListener g_search;
	bool g_searching = false, g_searchWanted = false;
	std::string g_hostAddress, g_removePrompt;

	void Search(bool on)
	{
		if (on == g_searching)
			return;
		g_searching = on;
		if (!on)
		{
			g_search.stop();
			return;
		}
		std::string err;
		if (!g_search.start(ob::server::kBeaconPort, &err))
		{
			g_searching = false;
			Note("Can't look for nearby games: " + err);
		}
	}

	bool Hosting() { return g_host && g_host->running(); }

	std::string HostName()
	{
		const std::string saved = Ui("host_name");
		const char* pc = std::getenv("COMPUTERNAME");
		return obui::cleanHostName(!saved.empty() ? saved : pc ? pc : "");
	}

	void StartHosting()
	{
		std::string address;
		int prefix = 0;
		if (!ob::server::findLocalNetwork(address, prefix))
		{
			Note("No local network found: connect to Wi-Fi or Ethernet to host");
			return;
		}
		ob::server::HostOptions o;
		o.name = HostName();
		o.address = address;
		o.prefix = prefix; // only this network may join (security queue S10)
		o.webPort = 443; // a PC may listen on 443: players join with no remap
		o.log = [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); };
		const std::string bc = obui::broadcastAddress(address, prefix);
		if (!bc.empty() && bc != "255.255.255.255")
			o.beaconTargets.push_back(bc);
		if (!g_host)
			g_host = std::make_unique<ob::server::LocalHost>();
		std::string err;
		if (!g_host->start(o, &err))
		{
			o.webPort = 18443; // 443 taken by another program: the port joiners remap to (the beacon tells them)
			if (!g_host->start(o, &err))
			{
				Note("Hosting didn't start: " + err);
				return;
			}
		}
		g_hostAddress = address;
		SetUi("net_join", address + ":" + std::to_string(g_host->webPort()), "hosting");
		Note("Hosting on " + address);
	}

	void PageNetwork()
	{
		const std::string mode = Ui("net_mode", "off");
		int m = mode == "local" ? 1 : mode == "server" ? 2 : 0;
		BeginCard("ONLINE PLAY");
		const char* const modes[] = {"Off", "Local play", "Fan server"};
		if (Choice("Online play", m, modes, 3, s->launcher))
			SetUi("net_mode", m == 1 ? "local" : m == 2 ? "server" : "off", "online play");
		LaunchOnlyNote();
		const char* dev9 = std::getenv("PS2X_DEV9");
		if (dev9 && *dev9)
			Hint("This run's network comes from PS2X_DEV9 and the other PS2X_NET settings (a scripted run); these are used otherwise.");
		if (m == 0)
			Hint("The game plays offline.");
		if (m == 2)
		{
			std::string server = Ui("net_server");
			if (TextField("Server DNS address", server, s->launcher))
			{
				if (obui::isIpv4(server))
					SetUi("net_server", server, "server address");
				else
					Note("Not an address: use four numbers like 203.0.113.9");
			}
			Hint("Enter the DNS address the fan server's guide gives for PCSX2 (\"DNS1\"). Everyone on it plays together: "
				 "Outbreak Modern, PCSX2 and PS2.");
		}
		EndCard();

		if (m == 1)
		{
			g_searchWanted = s->launcher;
			Search(g_searchWanted);
			if (s->launcher)
			{
				BeginCard("NEARBY GAMES");
				int shown = 0;
				for (const ob::server::FoundHost& h : g_search.hosts())
				{
					if (Hosting() && h.address == g_hostAddress)
						continue; // this PC's own game: "Play on this host" below
					shown++;
					const std::string label = obui::cleanHostName(h.info.name) + "  -  " + std::to_string(h.info.players) +
						(h.info.players == 1 ? " player, " : " players, ") + h.address;
					const char* const join[] = {"Join"};
					if (Actions(label.c_str(), join, 1) == 0 && obui::isLocalIpv4(h.address))
					{
						SetUi("net_join", h.address + ":" + std::to_string(h.info.webPort), "game to join");
						TryPlay();
					}
				}
				if (!shown)
					Hint("Looking for games on this network...");
				EndCard();
			}

			BeginCard("HOST A GAME");
			if (Hosting())
			{
				Info("Hosting", HostName() + " on " + g_hostAddress + " (" + std::to_string(g_host->players().size()) +
					" in the lobby)", &kSuccess);
				for (const std::string& p : g_host->players())
				{
					if (p == g_hostAddress)
					{
						Info(p.c_str(), "This device"); // can't remove itself (security queue 2, L4)
						continue;
					}
					const char* const remove[] = {"Remove"};
					if (Actions(p.c_str(), remove, 1) == 0)
					{
						g_removePrompt = p;
						OpenDialog("Remove this player?", p + "\n\nThey are disconnected and can't rejoin until you stop hosting.",
							{"Remove", "Cancel"}, [](int i) {
								if (i == 0 && g_host)
								{
									g_host->removePlayer(g_removePrompt);
									Note("Removed " + g_removePrompt);
								}
								g_removePrompt.clear();
							}, 1);
					}
				}
				const char* const both[] = {"Play on this host", "Stop hosting"};
				const char* const stop[] = {"Stop hosting"};
				const int a = s->launcher ? Actions(nullptr, both, 2) : Actions(nullptr, stop, 1);
				if (s->launcher && a == 0)
				{
					SetUi("net_join", g_hostAddress + ":" + std::to_string(g_host->webPort()), "game to join");
					TryPlay();
				}
				else if (a == (s->launcher ? 1 : 0))
				{
					g_host->stop();
					Note("Stopped hosting");
				}
			}
			else
			{
				std::string name = HostName();
				if (TextField("Host name", name, s->launcher))
					SetUi("host_name", obui::cleanHostName(name), "host name");
				const char* const host[] = {"Host a game"};
				if (Actions(nullptr, host, 1, s->launcher) == 0)
					StartHosting();
			}
			Hint("Players on the same network join from their Nearby games list. Windows may ask once whether Outbreak "
				 "Modern may use the network: allow it on private networks. Phones join here too; they can't host for PCSX2 "
				 "or PS2 players, who join a fan server.");
			EndCard();
		}

		if (m != 0)
		{
			BeginCard("CONNECTION");
			const char* const how[] = {"Automatic (DHCP)", "Manual"};
			int c = Ui("net_connection") == "manual" ? 1 : 0;
			if (Choice("Network settings", c, how, 2, s->launcher))
				SetUi("net_connection", c == 1 ? "manual" : "dhcp", "network settings");
			if (c == 1)
			{
				const char* const keys[] = {"net_address", "net_netmask", "net_gateway", "net_dns1", "net_dns2"};
				const char* const labels[] = {"IP address", "Subnet mask", "Gateway", "DNS 1", "DNS 2 (optional)"};
				for (int i = 0; i < 5; i++)
				{
					std::string v = Ui(keys[i]);
					if (TextField(labels[i], v, s->launcher))
					{
						if (obui::isIpv4(v) || (i == 4 && v.empty()))
							SetUi(keys[i], v, labels[i]);
						else
							Note("Not an address: use four numbers like 192.168.0.80");
					}
				}
				obui::NetConnection nc{false, Ui("net_address"), Ui("net_netmask"), Ui("net_gateway"), Ui("net_dns1"), Ui("net_dns2")};
				if (obui::netcnfValue(nc).empty())
					Info(nullptr, "Fill in every address above, or the game plays offline.", &kDanger);
			}
			Hint("The game's own network settings. Automatic suits almost every home network.");
			EndCard();
		}
	}

	// At Play: the Network page's choices become the game's environment (core/net_choice.h), read when the runtime
	// starts the network adapter (after the launcher). A scripted run that set PS2X_DEV9 itself keeps its own.
	void ApplyNetwork()
	{
		const char* dev9 = std::getenv("PS2X_DEV9");
		if (dev9 && *dev9)
			return;
		const obui::EnvList env = obui::chosenNetEnv([](const std::string& k) { return Ui(k.c_str()); }, "");
		for (const auto& [key, value] : env)
#if defined(_WIN32)
			_putenv_s(key.c_str(), value.c_str());
#else
			setenv(key.c_str(), value.c_str(), 1);
#endif
		if (!env.empty())
			std::fprintf(stderr, "[ui] online play: %s\n", Ui("net_mode").c_str());
	}
#else
	void PageNetwork() {}
	void ApplyNetwork() {}
#endif

#if defined(__ANDROID__)
	std::string ReadSmall(const fs::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
	// A JSON string field, enough for meta.json's flat keys.
	std::string JsonField(const std::string& json, const char* key)
	{
		const std::string k = std::string("\"") + key + "\"";
		size_t p = json.find(k);
		if (p == std::string::npos)
			return {};
		p = json.find(':', p);
		if (p == std::string::npos)
			return {};
		p = json.find_first_not_of(" \t\r\n", p + 1);
		if (p == std::string::npos)
			return {};
		if (json[p] == '"')
		{
			const size_t e = json.find('"', p + 1);
			return e == std::string::npos ? std::string() : json.substr(p + 1, e - p - 1);
		}
		const size_t e = json.find_first_of(",}\r\n", p);
		return json.substr(p, e - p);
	}
#endif

	void PageDriver()
	{
#if defined(__ANDROID__)
		BeginCard("VULKAN DRIVER");
		const std::string cur = Get("gpu_driver", "system");
		if (Pick("System driver (recommended)", cur == "system" || cur.empty(), s->launcher))
			Set("gpu_driver", "system", "graphics driver");
		const fs::path root = fs::u8path(s->host.filesDir) / "drivers";
		std::error_code ec;
		const int api = android_get_device_api_level();
		const bool adreno = access("/dev/kgsl-3d0", F_OK) == 0;
		for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_directory(ec))
				continue;
			const std::string name = U8(it->path().filename());
			const std::string meta = ReadSmall(it->path() / "meta.json");
			std::string why;
			if (meta.empty())
				why = "meta.json missing";
			else if (!adreno)
				why = "needs an Adreno GPU";
			else if (std::atoi(JsonField(meta, "minApi").c_str()) > api)
				why = "needs Android API " + JsonField(meta, "minApi");
			else
			{
				const std::string lib = JsonField(meta, "libraryName");
				if (!lib.empty() && !fs::exists(it->path() / lib, ec))
					why = lib + " missing";
			}
			const std::string label = name + "  " + JsonField(meta, "name") + " " + JsonField(meta, "driverVersion") +
			                          (why.empty() ? "" : "  (" + why + ")");
			if (Pick(label, cur == name, s->launcher && why.empty()))
				Set("gpu_driver", name, "graphics driver");
		}
		if (s->launcher)
		{
			const char* const btn[] = {"Add driver (.zip)"};
			if (Actions(nullptr, btn, 1) == 0)
				obui::android::startPick(obui::android::PickKind::DriverZip);
		}
		Hint("The Vulkan driver the game draws with. A change applies after you restart the app.");
		EndCard();
		BeginCard("LAST RUN");
		const std::string last = ReadSmall(fs::u8path(s->host.filesDir) / "gpu-last-run.txt");
		Info(nullptr, last.empty() ? std::string("Not known yet.") : last);
		EndCard();
		BeginCard("ADVANCED");
		const char* const reads[] = {"auto", "copy", "barrier", "fetch"};
		const std::string sr = Get("gs_self_read", "auto");
		int ri = 0;
		for (int i = 0; i < 4; i++)
			if (sr == reads[i])
				ri = i;
		if (Choice("Self-read (testing)", ri, reads, 4, s->launcher))
			Set("gs_self_read", reads[ri], "self-read");
		EndCard();
#endif
	}

	void PageAdvanced()
	{
		BeginCard("ENGINE");
		const char* const vu1[] = {"Default", "Recompiler", "Interpreter"};
		const std::string v = Get("vu1");
		int i = v == "mvu" ? 1 : v == "interp" ? 2 : 0;
		if (Choice("VU1 engine", i, vu1, 3, s->launcher))
			Set("vu1", i == 1 ? "mvu" : i == 2 ? "interp" : "", "VU1 engine");
		LaunchOnlyNote();
		Hint("The PS2's vector unit that transforms the 3D. Recompiler = fast (mvu), Interpreter = the reference. PS2X_VU1 wins over this.");
		EndCard();
	}

	// ---- Mods: the .pnach files in <mods>/<serial>/, turned off by renaming to .pnach.disabled ----
	// The runner loads only files whose extension is ".pnach" (PS2Recomp ps2_pnach.cpp:221, `extension() == ".pnach"`),
	// so "x.pnach.disabled" stays on disk and is skipped. The same convention as the Android launcher.
	// Widescreen mods (core/widescreen.h: the player's own widescreen .pnach or ob-widescreen.pnach) are the Display
	// page's Aspect ratio switch: never listed, counted, toggled or deleted here.
	struct ModFile
	{
		fs::path path;
		std::string name;  // shown
		std::string file;  // the file name without .pnach / .pnach.disabled
		bool enabled = true;
		uint64_t size = 0;
	};
	std::vector<ModFile> g_mods;

	fs::path ModsDir()
	{
		const std::string m = Get("mods");
		return m.empty() ? fs::path() : Resolve(m) / s->host.gameSerial;
	}

	std::string SizeText(uint64_t bytes)
	{
		char t[32];
		if (bytes >= (1ull << 30))
			std::snprintf(t, sizeof(t), "%.1f GB", bytes / double(1ull << 30));
		else if (bytes >= (1ull << 20))
			std::snprintf(t, sizeof(t), "%.1f MB", bytes / double(1ull << 20));
		else if (bytes >= 1024)
			std::snprintf(t, sizeof(t), "%.0f KB", bytes / 1024.0);
		else
			std::snprintf(t, sizeof(t), "%llu bytes", static_cast<unsigned long long>(bytes));
		return t;
	}

	// A name for the list: the file's first [section] (PCSX2 pnach files name their patch there, e.g. "Widescreen 16:9";
	// gametitle= names the game, the same for every mod), else its description=, else the file name.
	std::string ModTitle(const fs::path& p, const std::string& fallback)
	{
		std::ifstream f(p, std::ios::binary);
		std::string line, description;
		for (int n = 0; n < 60 && std::getline(f, line); n++)
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			const size_t a = line.find_first_not_of(" \t");
			if (a == std::string::npos)
				continue;
			line.erase(0, a);
			if (line.size() > 2 && line.front() == '[' && line.back() == ']')
				return line.substr(1, line.size() - 2);
			if (description.empty() && obui::toLower(line.substr(0, 12)) == "description=")
				description = line.substr(12);
		}
		return description.empty() ? fallback : description;
	}

	void ListMods()
	{
		g_mods.clear();
		s->modsListed = true;
		s->modsListedAt = s->now;
		const fs::path dir = ModsDir();
		std::error_code ec;
		if (dir.empty() || !fs::is_directory(dir, ec))
			return;
		for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			std::error_code e2;
			if (!it->is_regular_file(e2))
				continue;
			const std::string name = U8(it->path().filename());
			const std::string low = obui::toLower(name);
			ModFile m;
			if (low.size() > 6 && low.compare(low.size() - 6, 6, ".pnach") == 0)
				m.file = name.substr(0, name.size() - 6);
			else if (low.size() > 15 && low.compare(low.size() - 15, 15, ".pnach.disabled") == 0)
			{
				m.file = name.substr(0, name.size() - 15);
				m.enabled = false;
			}
			else
				continue;
			if (obui::isWidescreenModFile(it->path()))
				continue; // any widescreen mod belongs to Display > Aspect ratio (core/widescreen.h)
			m.path = it->path();
			m.size = it->file_size(e2);
			m.name = ModTitle(m.path, m.file);
			g_mods.push_back(std::move(m));
		}
		std::sort(g_mods.begin(), g_mods.end(),
			[](const ModFile& a, const ModFile& b) { return obui::toLower(a.name) < obui::toLower(b.name); });
	}

	void SetModEnabled(const ModFile& m, bool on)
	{
		const fs::path to = m.path.parent_path() / fs::u8path(m.file + (on ? ".pnach" : ".pnach.disabled"));
		std::error_code ec;
		if (fs::exists(to, ec))
		{
			Note("Not changed: " + U8(to.filename()) + " already exists");
			return;
		}
		fs::rename(m.path, to, ec);
		if (ec)
			Note("Not changed: " + ec.message());
		else
			Note(std::string(on ? "Turned on: " : "Turned off: ") + m.name + (s->launcher ? "" : " (applies next start)"));
		s->modsListed = false;
	}

	void PageMods()
	{
		BeginCard("MODS FOLDER");
		const std::string mods = Get("mods");
		Info(nullptr, mods.empty() ? std::string("None") : U8(Resolve(mods)));
		if (s->launcher)
		{
			const char* const btn[] = {s->host.android ? "Import .zip" : "Browse...", "Clear"};
			const int a = Actions(nullptr, btn, 2);
			if (a == 0)
			{
#if defined(__ANDROID__)
				obui::android::startPick(obui::android::PickKind::ModsZip);
#else
				Browse("Choose the mods folder (the one that holds SLPM-65428)", true, "", nullptr, nullptr, Resolve(mods),
					[](const std::string& p) {
						Set("mods", p, "mods folder");
						s->gsChanged = true;
						s->modsListed = false;
					});
#endif
			}
			else if (a == 1 && !mods.empty())
			{
				Set("mods", "", "mods folder");
				s->gsChanged = true;
				s->modsListed = false;
			}
		}
		else
			Hint("Change the folder in the launcher.");
		EndCard();

		if (!s->modsListed || s->now - s->modsListedAt > 2.0)
			ListMods();
		BeginCard("INSTALLED MODS");
		if (!s->launcher)
			Hint("Changes apply the next time the game starts.");
		if (mods.empty())
			Hint("No mods folder yet. Choose one above.");
		else if (g_mods.empty())
			Hint(("No mods yet. Put .pnach files in " + U8(ModsDir()) + ".").c_str());
		else if (Steering())
			Hint(s->mode == Mode::Pad ? "Left / right or A: on or off.   Y: delete." : "Left / right or Enter: on or off.");
		const std::vector<ModFile> list = g_mods; // a change below relists
		for (const ModFile& m : list)
		{
			Row r = BeginRow();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(m.name.c_str());
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, kDim);
			ImGui::Text("  %s  -  %s", (m.file + ".pnach").c_str(), SizeText(m.size).c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine();
			// On / Off, then Delete, right-aligned in the card.
			const float gap = 0.25f * s->px;
			const float w = std::min(ControlWidth(), 12.0f * s->px);
			const float del = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetCursorScreenPos(ImVec2(s->cardX + s->cardW - 0.9f * s->px - w - gap * 2.0f - del, ImGui::GetCursorScreenPos().y));
			bool on = m.enabled;
			if (const int d = RowStep(r))
				on = d > 0;
			if (RowA(r))
				on = !on;
			const char* const names[] = {"Off", "On"};
			for (int i = 0; i < 2; i++)
			{
				if (i)
					ImGui::SameLine(0, gap);
				const bool sel = (on ? 1 : 0) == i;
				if (sel)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
				}
				if (ImGui::Button(names[i], ImVec2((w - gap) / 2, 0)))
					on = i == 1;
				if (sel)
					ImGui::PopStyleColor(2);
			}
			ImGui::SameLine(0, gap * 2.0f);
			bool remove = ImGui::Button("Delete");
			if (r.focused && s->evY)
			{
				s->evY = false;
				remove = true;
			}
			EndRow(r);
			if (on != m.enabled)
				SetModEnabled(m, on);
			if (remove)
			{
				s->modPrompt = U8(m.path);
				OpenDialog("Delete mod?",
					"Delete " + m.name + " (" + U8(m.path.filename()) + ", " + SizeText(m.size) + ")?\nThis can't be undone.",
					{"Delete", "Cancel"}, [](int i) {
						if (i == 0)
						{
							std::error_code ec;
							fs::remove(fs::u8path(s->modPrompt), ec);
							Note(ec ? "Not deleted: " + ec.message() : std::string("Mod deleted"));
						}
						s->modPrompt.clear();
						s->modsListed = false;
					},
					1); // Cancel has the focus
			}
		}
		EndCard();
	}

	void DrawPage()
	{
		switch (s->page)
		{
			case Page::Game: PageGame(); break;
			case Page::Controls: PageControls(); break;
			case Page::Camera: PageCamera(); break;
			case Page::Display: PageDisplay(); break;
			case Page::Mods: PageMods(); break;
			case Page::Cards: PageCards(); break;
			case Page::Audio: PageAudio(); break;
			case Page::Network: PageNetwork(); break;
			case Page::Driver: PageDriver(); break;
			case Page::Advanced: PageAdvanced(); break;
			default: break;
		}
	}

	// ---- look: fonts, style, scale ----
	ImFont* g_pendingText = nullptr;
	ImFont* g_pendingBold = nullptr;
	void LoadFonts()
	{
		// A GS reopen (the game's options differ from the launcher's: ob_gs_reopen_if_changed) destroys ImGui's context
		// with its fonts (GSclose -> ImGuiManager::Shutdown) and makes a new one: the fonts and style here then point at
		// freed memory (developer, 2026-10-26: Start + Select, the menu, crashed on Windows). Start over on a new context.
		static ImGuiContext* s_ctx = nullptr;
		if (s_ctx != ImGui::GetCurrentContext())
		{
			s_ctx = ImGui::GetCurrentContext();
			s->fontsTried = false;
			s->styleReady = false;
			s->fontText = s->fontBold = g_pendingText = g_pendingBold = nullptr;
		}
		if (s->fontsTried)
		{
			// Added on the first frame, drawn with from the next.
			s->fontText = g_pendingText;
			s->fontBold = g_pendingBold;
			ob_gs_set_overlay_icon_font(s->fontText); // the hold ring's gear (icons are merged into the text font)
			return;
		}
		s->fontsTried = true;
		ImGuiIO& io = ImGui::GetIO();
		std::error_code ec;
#if defined(_WIN32)
		char windir[260] = {};
		const char* w = std::getenv("WINDIR");
		std::snprintf(windir, sizeof(windir), "%s\\Fonts\\", w && *w ? w : "C:\\Windows");
		const std::string dir = windir;
		const std::string text = dir + "segoeui.ttf", bold = dir + "seguisb.ttf";
		std::string icons = dir + "SegoeIcons.ttf";
		if (!fs::exists(icons, ec))
			icons = dir + "segmdl2.ttf";
		if (fs::exists(text, ec))
		{
			g_pendingText = io.Fonts->AddFontFromFileTTF(text.c_str(), 18.0f);
			if (g_pendingText && fs::exists(icons, ec))
			{
				ImFontConfig cfg;
				cfg.MergeMode = true;
				cfg.GlyphOffset = ImVec2(0, 2.0f);
				io.Fonts->AddFontFromFileTTF(icons.c_str(), 18.0f, &cfg);
			}
		}
		if (fs::exists(bold, ec))
		{
			g_pendingBold = io.Fonts->AddFontFromFileTTF(bold.c_str(), 18.0f);
			if (g_pendingBold && fs::exists(icons, ec))
			{
				ImFontConfig cfg;
				cfg.MergeMode = true; // the Play button and titles draw icons in the bold face too
				cfg.GlyphOffset = ImVec2(0, 2.0f);
				io.Fonts->AddFontFromFileTTF(icons.c_str(), 18.0f, &cfg);
			}
		}
		std::fprintf(stderr, "[ui] fonts: text=%s bold=%s icons=%s\n", g_pendingText ? "segoeui" : "default",
			g_pendingBold ? "seguisb" : "default", fs::exists(icons, ec) ? fs::path(icons).filename().string().c_str() : "none");
#endif
		if (!g_pendingBold)
			g_pendingBold = g_pendingText;
	}

	void ApplyStyle()
	{
		ImGuiIO& io = ImGui::GetIO();
		// Body text from the drawn height: 18 px at 720p, 27 at 1080p, never under 17 (Windows handhelds are
		// 720p-1200p), times the player's Text size ([ui] text_size: normal / large / larger).
		const std::string ts = Ui("text_size", "normal");
		const float user = ts == "larger" ? 1.3f : ts == "large" ? 1.15f : 1.0f;
		s->px = std::round(std::max(17.0f, io.DisplaySize.y * 0.025f) * user);
		if (!s->styleReady)
		{
			ImGuiStyle st;
			ImGui::StyleColorsDark(&st);
			st.WindowRounding = 0.0f;
			st.ChildRounding = 8.0f;
			st.FrameRounding = 6.0f;
			st.PopupRounding = 8.0f;
			st.GrabRounding = 6.0f;
			st.ScrollbarRounding = 8.0f;
			st.WindowBorderSize = 0.0f;
			st.ChildBorderSize = 0.0f;
			st.FrameBorderSize = 0.0f;
			st.ItemSpacing = ImVec2(8, 7);
			st.FramePadding = ImVec2(10, 5);
			st.WindowPadding = ImVec2(14, 12);
			ImVec4* c = st.Colors;
			c[ImGuiCol_WindowBg] = ImVec4(0.086f, 0.094f, 0.114f, 1.0f);
			c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
			c[ImGuiCol_PopupBg] = ImVec4(0.125f, 0.141f, 0.173f, 1.0f);
			c[ImGuiCol_Text] = ImVec4(0.910f, 0.918f, 0.929f, 1.0f);
			c[ImGuiCol_TextDisabled] = kDim;
			c[ImGuiCol_Button] = ImVec4(0.165f, 0.184f, 0.220f, 1.0f);
			c[ImGuiCol_ButtonHovered] = ImVec4(0.204f, 0.227f, 0.271f, 1.0f);
			c[ImGuiCol_ButtonActive] = ImVec4(0.239f, 0.267f, 0.314f, 1.0f);
			c[ImGuiCol_FrameBg] = ImVec4(0.149f, 0.169f, 0.200f, 1.0f);
			c[ImGuiCol_FrameBgHovered] = ImVec4(0.184f, 0.208f, 0.247f, 1.0f);
			c[ImGuiCol_FrameBgActive] = ImVec4(0.216f, 0.243f, 0.290f, 1.0f);
			c[ImGuiCol_SliderGrab] = kAccent;
			c[ImGuiCol_SliderGrabActive] = kAccentHover;
			c[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
			c[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.05f); // hover: a faint tint, never the selected look
			c[ImGuiCol_HeaderActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.09f);
			c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.08f);
			c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
			c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
			c[ImGuiCol_CheckMark] = kAccent;
			s->baseStyle = st;
			s->styleReady = true;
		}
		ImGui::GetStyle() = s->baseStyle;
		ImGui::GetStyle().ScaleAllSizes(s->px / 15.0f);
	}

	// ---- input into ImGui (the mouse and text only: navigation is ours) ----
	void FeedInput()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
		// The host's pointer is in its window units; ImGui draws at the GS surface size. They differ on Android
		// (raylib keeps the size the runner asked for, the surface is the display's): scale between the two.
		const float sx = s->in.width > 0 ? io.DisplaySize.x / static_cast<float>(s->in.width) : 1.0f;
		const float sy = s->in.height > 0 ? io.DisplaySize.y / static_cast<float>(s->in.height) : 1.0f;
		if (s->mode == Mode::Mouse)
			io.AddMousePosEvent(s->in.mouseX * sx, s->in.mouseY * sy);
		else
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); // no hover highlight under a resting cursor while steering
		for (int b = 0; b < 3; b++)
			io.AddMouseButtonEvent(b, s->in.mouseDown[b]);
		if (s->in.wheel != 0.0f)
			io.AddMouseWheelEvent(0.0f, s->in.wheel);
		io.AddKeyEvent(ImGuiMod_Ctrl, s->in.ctrl);
		io.AddKeyEvent(ImGuiMod_Shift, s->in.shift);
		io.AddKeyEvent(ImGuiMod_Alt, s->in.alt);
		for (int c = 0; c < s->in.charCount; c++)
			io.AddInputCharacter(s->in.chars[c]);
		// Editing keys for text boxes (TextField); ImGui's own keyboard navigation stays off.
		for (int k = 0; k < s->in.keyCount; k++)
		{
			ImGuiKey key = ImGuiKey_None;
			switch (s->in.keys[k].code)
			{
				case ObUiNavKey::Left: key = ImGuiKey_LeftArrow; break;
				case ObUiNavKey::Right: key = ImGuiKey_RightArrow; break;
				case ObUiNavKey::Home: key = ImGuiKey_Home; break;
				case ObUiNavKey::End: key = ImGuiKey_End; break;
				case ObUiNavKey::Delete: key = ImGuiKey_Delete; break;
				case ObUiNavKey::Backspace: key = ImGuiKey_Backspace; break;
				case ObUiNavKey::Enter: key = ImGuiKey_Enter; break;
				default: break;
			}
			if (key != ImGuiKey_None)
				io.AddKeyEvent(key, s->in.keys[k].down);
		}
	}

	std::vector<Page> VisiblePages()
	{
		std::vector<Page> pages;
		for (int i = 0; i < static_cast<int>(Page::Count); i++)
		{
			const Page p = static_cast<Page>(i);
			if (p == Page::Driver && !s->host.android)
				continue;
			if (p == Page::Network && s->host.android)
				continue; // Android's Network page is the Kotlin launcher's (NetPlay.kt)
			if (!s->launcher && (p == Page::Game || p == Page::Cards || p == Page::Advanced || p == Page::Driver))
				continue; // launch-only pages live in the launcher
			pages.push_back(p);
		}
		return pages;
	}

	void TryPlay()
	{
		if (DiscOk())
			s->frameResult = 1;
		else
		{
			s->page = Page::Game;
			s->level = Level::List;
			Note(s->gamePath.empty() ? "Choose your game disc first" : s->disc.message);
		}
	}

	void AskQuit()
	{
		OpenDialog("Quit Outbreak Modern?", "", {"Quit", "Cancel"}, [](int i) {
			if (i == 0)
				s->frameResult = 2;
		});
	}

	// The step-in model on the list and footer levels (the page level is handled by the rows as they draw).
	void NavigateOuter(const std::vector<Page>& pages)
	{
		if (s->dialog.open || s->captureRow >= 0)
			return;
		size_t at = 0;
		for (size_t i = 0; i < pages.size(); i++)
			if (pages[i] == s->page)
				at = i;
		auto selectPage = [&](size_t i) {
			s->page = pages[i];
			s->subFocus = 0;
			g_browser.open = false;
		};
		// L1 / R1: previous / next page from any level (inside a page: into the same row slot of the new page).
		if ((s->evL1 || s->evR1) && !g_browser.open)
		{
			if (s->level == Level::Page)
				s->lastRow[static_cast<int>(s->page)] = s->rowFocus;
			selectPage((at + pages.size() + (s->evR1 ? 1 : -1)) % pages.size());
			if (s->level == Level::Page)
			{
				s->rowFocus = s->lastRow[static_cast<int>(s->page)];
				s->scrollToFocus = true;
			}
			else
				s->level = Level::List;
			s->evL1 = s->evR1 = false;
			return;
		}
		if (s->launcher && s->evStart && !g_browser.open)
		{
			s->evStart = false;
			TryPlay(); // Start = Play from anywhere
			return;
		}
		switch (s->level)
		{
			case Level::List:
				if (s->evUp && at > 0)
					selectPage(at - 1);
				else if (s->evDown)
				{
					if (at + 1 < pages.size())
						selectPage(at + 1);
					else
					{
						s->level = Level::Footer; // down past the last page: Quit / Play
						s->footerFocus = 1;
					}
				}
				else if (s->evA || s->evRight)
				{
					s->level = Level::Page; // step in
					s->rowFocus = s->lastRow[static_cast<int>(s->page)];
					s->subFocus = 0;
					s->scrollToFocus = true;
				}
				else if (s->evB)
				{
					if (s->launcher)
						AskQuit();
					else
						s->frameResult = 1; // B on the list in game = resume
				}
				ClearNav();
				break;
			case Level::Footer:
				if (s->evLeft)
					s->footerFocus = 0;
				else if (s->evRight)
					s->footerFocus = 1;
				else if (s->evUp || s->evB)
				{
					s->level = Level::List; // back to the last page entry
					selectPage(pages.size() - 1);
				}
				else if (s->evA)
				{
					if (s->footerFocus == 0)
					{
						if (s->launcher)
							AskQuit();
						else
							s->frameResult = 2;
					}
					else if (s->launcher)
						TryPlay();
					else
						s->frameResult = 1;
				}
				ClearNav();
				break;
			case Level::Page:
				if (g_browser.open)
				{
					if (s->evUp)
						s->rowFocus = std::max(0, s->rowFocus - 1);
					if (s->evDown)
						s->rowFocus = std::min(std::max(0, s->rowCount - 1), s->rowFocus + 1);
					if (s->evUp || s->evDown)
					{
						s->scrollToFocus = true;
						s->subFocus = 0;
					}
					s->evUp = s->evDown = false;
					break; // B / Y / A go to the browser and its rows
				}
				if (s->evB)
				{
					s->lastRow[static_cast<int>(s->page)] = s->rowFocus;
					s->level = Level::List; // step out
					ClearNav();
					break;
				}
				if (s->evUp && s->rowFocus > 0)
				{
					s->rowFocus--;
					s->subFocus = 0;
					s->scrollToFocus = true;
				}
				if (s->evDown && s->rowFocus + 1 < s->rowCount)
				{
					s->rowFocus++;
					s->subFocus = 0;
					s->scrollToFocus = true;
				}
				s->evUp = s->evDown = false;
				break; // left / right / A go to the focused row
		}
	}

	// ---- the two screens ----
	void Build(void*)
	{
		LoadFonts();
		FeedInput();
		ApplyStyle();
		const std::vector<Page> pages = VisiblePages();
		if (std::find(pages.begin(), pages.end(), s->page) == pages.end())
			s->page = pages.front();
		// A dialog takes this frame's input: keep it from the rows drawn before it.
		bool dlgEv[10] = {s->evUp, s->evDown, s->evLeft, s->evRight, s->evA, s->evB, s->evY, s->evL1, s->evR1, s->evStart};
		const bool dialogFirst = s->dialog.open;
		if (dialogFirst)
			ClearNav();
		else
			NavigateOuter(pages);

		const ImGuiIO& io = ImGui::GetIO();
		const ImVec2 full = io.DisplaySize;
		const float px = s->px;
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		                         ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (s->launcher)
		{
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(full);
		}
		else
		{
			ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), full, IM_COL32(0, 0, 0, 160));
			const ImVec2 size(std::min(full.x - 2.0f * px, 62.0f * px), std::min(full.y - 2.0f * px, 36.0f * px));
			ImGui::SetNextWindowPos(ImVec2((full.x - size.x) * 0.5f, (full.y - size.y) * 0.5f));
			ImGui::SetNextWindowSize(size);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.8f * px);
		}
		ImGui::PushFont(s->fontText, px);
		ImGui::Begin(s->launcher ? "Outbreak Modern##launcher" : "Game paused##menu", nullptr, flags);

		// Header: icon, title, game line; the disc state on the right (launcher).
		// The icon is drawn after the title group, centred on the group's middle line.
		const ImVec2 headIcon = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(2.4f * px, 0));
		ImGui::SameLine(0, 0.4f * px);
		ImGui::BeginGroup();
		ImGui::PushFont(s->fontBold, px * 1.25f);
		ImGui::TextUnformatted(s->launcher ? "Outbreak Modern" : "Game paused");
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, kDim);
		ImGui::TextUnformatted(s->launcher ? "Resident Evil Outbreak File #1  -  SLPM-65428" : "Sound and timers are stopped");
		ImGui::PopStyleColor();
		ImGui::EndGroup();
		{
			const float mid = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
			ImGui::PushFont(s->fontText, px * 1.6f);
			IconText(ImGui::GetWindowDrawList(), headIcon.x, mid, s->launcher ? 0xE7FC : 0xE769, nullptr,
				ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::PopFont();
		}
		if (s->launcher && s->discChecked)
		{
			const bool ok = DiscOk();
			const unsigned icon = ok ? 0xE73E : 0xE7BA;
			const char* text = ok ? "Disc verified" : s->gamePath.empty() ? "No disc" : "Disc not verified";
			const float w = IconTextWidth(icon, text);
			ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x - 0.4f * px);
			ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			IconText(ImGui::GetWindowDrawList(), a.x, (a.y + b.y) * 0.5f, icon, text, ImGui::GetColorU32(ok ? kSuccess : kWarn));
		}
		ImGui::Dummy(ImVec2(0, 0.3f * px));
		ImGui::Separator();

		const float footer = 2.9f * px;
		// Page list: icon + name; the current page is filled, the focused entry (steering, list level) outlined.
		ImGui::BeginChild("##nav", ImVec2(13.0f * px, -footer), ImGuiChildFlags_None);
		for (const Page p : pages)
		{
			const int i = static_cast<int>(p);
			const bool cur = s->page == p;
			const std::string id = std::string("##tab-") + kPageNames[i];
			if (cur)
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(kAccent.x, kAccent.y, kAccent.z, s->level == Level::Page ? 0.22f : 0.35f));
			const bool clicked = ImGui::Selectable(id.c_str(), cur, 0, ImVec2(0, 1.9f * px));
			{
				// The icon and the name, each centred on the row's middle line.
				const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
				IconText(ImGui::GetWindowDrawList(), a.x + ImGui::GetStyle().FramePadding.x, (a.y + b.y) * 0.5f, kPageIcons[i],
					kPageNames[i], ImGui::GetColorU32(ImGuiCol_Text));
			}
			if (clicked)
			{
				if (s->level == Level::Page)
					s->lastRow[static_cast<int>(s->page)] = s->rowFocus;
				s->page = p;
				s->level = Level::List;
				s->captureRow = -1;
				g_browser.open = false;
			}
			if (cur)
				ImGui::PopStyleColor();
			if (cur && Steering() && s->level == Level::List && !s->dialog.open)
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), kFocusEdge, 0.4f * px, std::max(1.5f, px / 10.0f));
		}
		ImGui::EndChild();
		ImGui::SameLine(0, 0.8f * px);

		// The page: cards on channel 0, the focus on 1, the content on 2.
		ImGui::BeginChild("##page", ImVec2(0, -footer), ImGuiChildFlags_None);
		s->pageDl = ImGui::GetWindowDrawList();
		s->pageDl->ChannelsSplit(3);
		s->pageDl->ChannelsSetCurrent(2);
		s->rowCounter = 0;
		if (g_browser.open)
			DrawBrowser();
		else
			DrawPage();
		s->rowCount = s->rowCounter;
		if (s->rowFocus >= s->rowCount)
			s->rowFocus = std::max(0, s->rowCount - 1);
		ImGui::Dummy(ImVec2(0, 0.5f * px));
		s->pageDl->ChannelsMerge();
		s->pageDl = nullptr;
		ImGui::EndChild();
		// A step-in on a page with nothing to focus: back to the list.
		if (s->level == Level::Page && s->rowCount == 0 && !g_browser.open)
			s->level = Level::List;

		// Footer: status or prompts on the left, Quit and Play (Quit game and Resume) on the right.
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, 0.15f * px));
		const float bw = 8.5f * px, bh = 1.9f * px;
		const float textRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - bw * 2.0f - 1.5f * px - ImGui::GetStyle().WindowPadding.x;
		ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(textRight, ImGui::GetCursorScreenPos().y + bh + px), true);
		ImGui::BeginGroup();
		ImGui::Dummy(ImVec2(0, 0.35f * px));
		if (s->now < s->statusUntil)
			ImGui::TextUnformatted(s->status.c_str());
		else if (Steering())
		{
			const bool pad = s->mode == Mode::Pad;
			std::string prompt;
			if (s->dialog.open)
				prompt = pad ? "A  choose     B  cancel" : "Enter  choose     Esc  cancel";
			else if (s->captureRow >= 0)
				prompt = pad ? "Press the new button" : "Press the new key     Esc  cancel";
			else if (g_browser.open)
				prompt = pad ? "A  open     Y  use this folder     B  up one level" : "Enter  open     Esc  up one level";
			else if (s->level == Level::List)
				prompt = pad ? (s->launcher ? "A  open     L1 / R1  page     B  quit     Start  play" : "A  open     L1 / R1  page     B  resume")
				             : "Enter  open     Tab  page     Esc  back";
			else if (s->level == Level::Page)
				prompt = pad ? "A  select     Left / Right  change     B  back" : "Enter  select     Left / Right  change     Esc  back";
			else
				prompt = pad ? "A  choose     Up  back" : "Enter  choose     Up  back";
			ImGui::PushStyleColor(ImGuiCol_Text, kDim);
			ImGui::TextUnformatted(prompt.c_str());
			ImGui::PopStyleColor();
		}
		else if (s->launcher && !DiscOk())
			ImGui::TextUnformatted(s->gamePath.empty() ? "Choose your game disc to play" : "Disc not verified");
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kDim);
			ImGui::TextUnformatted(s->launcher ? "Settings save as you change them" : "F1 or Esc to close");
			ImGui::PopStyleColor();
		}
		ImGui::EndGroup();
		ImGui::PopClipRect();
		ImGui::SameLine(ImGui::GetWindowWidth() - bw * 2.0f - 0.5f * px - ImGui::GetStyle().WindowPadding.x);
		const bool footerFocus = Steering() && s->level == Level::Footer && !s->dialog.open;
		// Footer focus must read at a glance (developer, 2026-10-26: the ring around the accent Play was invisible): a
		// thick white border on the button and a soft accent halo outside it.
		auto focusRing = [&](bool on) {
			if (!on)
				return;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			const float t = std::max(3.0f, px / 6.0f), halo = 2.0f * t;
			dl->AddRect(ImVec2(a.x - halo, a.y - halo), ImVec2(b.x + halo, b.y + halo),
				ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f)), 0.4f * px + halo, t);
			dl->AddRect(a, b, IM_COL32(255, 255, 255, 255), 0.4f * px, t);
		};
		if (ImGui::Button(s->launcher ? "Quit" : "Quit game", ImVec2(bw, bh)))
		{
			if (s->launcher)
				AskQuit();
			else
				s->frameResult = 2;
		}
		focusRing(footerFocus && s->footerFocus == 0);
		ImGui::SameLine(0, 0.5f * px);
		const bool ready = !s->launcher || DiscOk();
		const bool playFocused = footerFocus && s->footerFocus == 1;
		ImGui::PushStyleColor(ImGuiCol_Button, playFocused ? kAccentHover : ready ? kAccent : ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ready ? kAccentHover : ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f));
		ImGui::PushFont(s->fontBold, px * 1.05f);
		const char* playText = s->launcher ? "Play" : "Resume";
		const bool playClicked = ImGui::Button("##play", ImVec2(bw, bh));
		{
			// Icon + label centred in the button, each by its own ink.
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			const float w = IconTextWidth(0xE768, playText);
			IconText(ImGui::GetWindowDrawList(), std::floor((a.x + b.x - w) * 0.5f), (a.y + b.y) * 0.5f, 0xE768, playText,
				ImGui::GetColorU32(ImGuiCol_Text));
		}
		if (playClicked)
		{
			// Play: dim until the disc is verified; a press then shows why (plan P9) instead of doing nothing.
			if (s->launcher)
				TryPlay();
			else
				s->frameResult = 1;
		}
		ImGui::PopFont();
		ImGui::PopStyleColor(2);
		focusRing(footerFocus && s->footerFocus == 1);
		ImGui::End();
		if (!s->launcher)
			ImGui::PopStyleVar();

		if (s->dialog.open)
		{
			if (dialogFirst)
			{
				s->evUp = dlgEv[0], s->evDown = dlgEv[1], s->evLeft = dlgEv[2], s->evRight = dlgEv[3], s->evA = dlgEv[4];
				s->evB = dlgEv[5], s->evY = dlgEv[6], s->evL1 = dlgEv[7], s->evR1 = dlgEv[8], s->evStart = dlgEv[9];
			}
			else
				ClearNav(); // opened this frame: its first input is the next one
			DrawDialog();
		}
		ImGui::PopFont();
	}

#if defined(__ANDROID__)
	// Picks from the Java bridge (native/ob_ui/android): one result a frame. (Android's ImGui launcher is being
	// replaced by the Kotlin one, decision A14; kept compiling meanwhile.)
	void PollAndroidPick()
	{
		obui::android::PickResult r;
		if (!obui::android::pollPick(r) || r.cancelled || r.fd < 0)
			return;
		const fs::path files = fs::u8path(s->host.filesDir);
		std::string err;
		std::error_code ec;
		switch (r.kind)
		{
			case obui::android::PickKind::Disc:
			{
				if (!obui::android::isSeekableFile(r.fd))
				{
					Note("That disc is not a file on this device (a cloud document?). Copy it to the device first.");
					close(r.fd);
					break;
				}
				if (s->discFd >= 0)
					close(s->discFd);
				s->discFd = r.fd; // kept open for the session: the game reads it through /proc/self/fd
				ChooseDisc(obui::android::fdPath(r.fd), false);
				if (DiscOk())
				{
					std::ofstream(files / "game-uri.txt", std::ios::binary) << r.uri;
					Note("Disc saved: " + r.displayName);
				}
				break;
			}
			case obui::android::PickKind::TexturesZip:
			{
				// PCSX2 reads <root>/<serial>.zip as a pack: copy it in as is.
				fs::create_directories(files / "textures", ec);
				if (obui::android::copyFdToFile(r.fd, files / "textures" / (s->host.gameSerial + ".zip"), &err))
				{
					Set("textures", "textures", "texture pack");
					s->gsChanged = true;
				}
				else
					Note("Import failed: " + err);
				close(r.fd);
				break;
			}
			case obui::android::PickKind::ModsZip:
			case obui::android::PickKind::DriverZip:
			{
				const bool driver = r.kind == obui::android::PickKind::DriverZip;
				const fs::path tmp = files / "import.zip";
				std::string stem = U8(fs::u8path(r.displayName).stem());
				for (char& c : stem)
					if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
						c = '_';
				const fs::path dest = driver ? files / "drivers" / (stem.empty() ? "driver" : stem)
				                             : files / "mods" / s->host.gameSerial;
				fs::create_directories(dest, ec);
				const bool ok = obui::android::copyFdToFile(r.fd, tmp, &err) &&
				                obui::android::extractZip(tmp, dest,
					                [driver](const std::string& n) {
						                return driver || (n.size() > 6 && n.compare(n.size() - 6, 6, ".pnach") == 0);
					                }, &err);
				fs::remove(tmp, ec);
				close(r.fd);
				if (!ok)
					Note("Import failed: " + err);
				else if (driver)
					Note("Driver added: choose it in the list, then restart the app");
				else
				{
					Set("mods", "mods", "mods folder");
					s->gsChanged = true;
				}
				break;
			}
			default:
				close(r.fd);
				break;
		}
	}
#endif

	void Present(const ObUiInput& in)
	{
		s->in = in;
		s->now = in.time;
		s->frameResult = 0;
		ReadNav();
		ob_gs_ui_present(&Build, nullptr);
	}
} // namespace

namespace obui
{
	void init(const ObUiHost& host)
	{
		if (!s)
			s = new State();
		s->host = host;
		if (s->host.gameSerial.empty())
			s->host.gameSerial = "SLPM-65428";
		s->ini.load(fs::u8path(host.settingsFile));
		s->input.load(fs::u8path(host.inputFile));
#if defined(__ANDROID__)
		obui::android::init();
#endif
	}

	bool prepare(std::string& gamePath, bool interactive)
	{
		if (!s)
			return true;
		// Widescreen is the default (developer, 2026-10-26): until the player has chosen an aspect ratio ([ui]
		// widescreen unset), turn the 16:9 mod on (their own widescreen .pnach if any, else ob-widescreen.pnach), so the
		// default really plays widescreen. Before the runner reads the mods (patch 0050). A saved 4:3 is kept.
		// Only with a visible window: scripted and hidden runs (the gate) keep their settings file untouched.
		if (interactive && !s->ini.get("ui", "widescreen"))
		{
			if (Get("mods").empty())
				Set("mods", "mods", "mods folder");
			std::string err;
			if (obui::setWidescreen(Resolve(Get("mods")) / s->host.gameSerial, true, &err))
				SetUi("widescreen", "on", "aspect ratio (default 16:9)");
			else
				std::fprintf(stderr, "[ui] default widescreen not set: %s\n", err.c_str());
		}
		// Start-up settings the menu owns ([ui]). The window mode waits for the game (ApplyGameWindow).
		if (s->host.setVolume)
			s->host.setVolume(std::atoi(Ui("volume", "100").c_str()) / 100.0f);
		const std::string vs = Ui("vsync", "on");
		ob_gs_set_vsync(vs == "strict" ? 2 : vs == "off" ? 0 : 1);
		ob_gs_set_fps_overlay(Ui("show_fps", "off") == "on");
#if defined(__ANDROID__)
		obui::android::perfHintEnable(Ui("perf_hint", "on") != "off");
#endif

		s->gamePath = gamePath;
#if defined(__ANDROID__)
		// The Kotlin launcher's OutbreakActivity opened the disc and passes /proc/self/fd/<n> (its fd kept open).
		const char* kotlinGame = std::getenv("PS2X_GAME_PATH");
		if (kotlinGame && *kotlinGame)
			s->gamePath = kotlinGame;
		// Else the disc picked earlier: reopen its document (permission kept) and read it through its descriptor.
		std::ifstream uriFile(fs::u8path(s->host.filesDir) / "game-uri.txt", std::ios::binary);
		std::string uri;
		if (!(kotlinGame && *kotlinGame))
			std::getline(uriFile, uri);
		if (!uri.empty())
		{
			const int fd = obui::android::openDocument(uri);
			if (fd >= 0 && obui::android::isSeekableFile(fd))
			{
				s->discFd = fd;
				s->gamePath = obui::android::fdPath(fd);
			}
			else if (fd >= 0)
				close(fd);
		}
#endif
		CheckDisc();
		gamePath = s->gamePath;
		if (!interactive)
			return true;
		const bool skip = Ui("launcher", "on") == "off" && DiscOk();
		if (skip)
		{
			ApplyNetwork(); // straight into the game: the launcher's Play does this otherwise
			ApplyGameWindow();
		}
		return skip;
	}

	int launcherFrame(const ObUiInput& in, std::string& gamePath)
	{
		s->launcher = true;
		if (!s->discChecked)
			CheckDisc();
#if defined(__ANDROID__)
		PollAndroidPick();
#endif
#if !defined(__ANDROID__)
		g_searchWanted = false;
#endif
		Present(in);
#if !defined(__ANDROID__)
		if (!g_searchWanted || s->frameResult != 0)
			Search(false); // not on the Network page's local play (or leaving the launcher): stop listening
#endif
		if (s->frameResult == 1)
		{
			ApplyNetwork();
			ApplyGameWindow();
			if (Ui("card_backup", "on") == "on")
			{
				const char* env = std::getenv("PS2X_SAVES");
				if (!(env && *env))
				{
					const fs::path folder = Get("saves").empty() ? SettingsDir() / "memcards" : Resolve(Get("saves"));
					std::string err;
					if (fs::exists(folder / kSlotCardNames[0]))
						backupCard(folder / kSlotCardNames[0], 5, &err);
				}
			}
			gamePath = s->gamePath;
			s->launcher = false;
			std::fprintf(stderr, "[ui] play %s (settings%s changed for the GS)\n", gamePath.c_str(), s->gsChanged ? "" : " not");
		}
		return s->frameResult;
	}

	bool gsOptionsChanged()
	{
		const bool c = s && s->gsChanged;
		if (s)
			s->gsChanged = false;
		return c;
	}

	int hostFrame(const ObUiInput& in)
	{
		if (!s)
			return 0;
		s->launcher = false;
		s->now = in.time;
		// Esc belongs to the menu's own "back" while inside a page, the browser, a dialog or a capture; on the page
		// list it closes the menu (as F1, and on Windows holding Back + Start for 2 s).
		const bool escIsBack = s->menuOpen && (s->level != Level::List || g_browser.open || s->dialog.open || s->captureRow >= 0);
		const bool toggle = s->trigger.update(in.time, in.backDown, in.startDown, in.guideDown,
			in.f1Pressed && s->captureRow < 0, in.escPressed && !escIsBack);
		bool open = s->menuOpen;
		if (toggle)
			open = !open;
		// The hold ring while Back + Start are held toward the 2 s (Windows; 0 = hidden). Not over the open menu.
		ob_gs_set_hold_progress(s->menuOpen || open ? 0.0f : s->trigger.comboProgress(in.time));
#if defined(__ANDROID__)
		// Android: the menu is Kotlin + Compose (android_menu.h, decision A14), a dialog window over the game; the
		// game is held while it is open. An APK without it falls back to the ImGui menu below.
		{
			obui::android::perfHintFrame(); // the game thread's CPU time this frame, to Android's performance hint
			const obui::android::MenuClose c = obui::android::takeMenuClose();
			if (c.closed && s->kotlinMenu)
			{
				s->kotlinMenu = false;
				s->menuOpen = false;
				s->trigger.setMenuOpen(false, in.time);
				if (c.inputChanged && s->host.reloadInputBindings)
					s->host.reloadInputBindings();
				if (c.settingsChanged)
				{
					s->ini.load(fs::u8path(s->host.settingsFile));
					const std::string vs = Ui("vsync", "on");
					ob_gs_set_vsync(vs == "strict" ? 2 : vs == "off" ? 0 : 1);
					ob_gs_set_fps_overlay(Ui("show_fps", "off") == "on");
					if (s->host.setVolume)
						s->host.setVolume(std::atoi(Ui("volume", "100").c_str()) / 100.0f);
				}
				std::fprintf(stderr, "[ui] menu closed (Kotlin)%s\n", c.quit ? ": quit" : "");
				if (c.quit)
					return 2;
				return 0;
			}
			if (s->kotlinMenu)
				return 1; // open: hold the game (a toggle while open is Kotlin's to handle)
			// The touch controls' Menu > Settings (TouchOverlay): the same as the Back button.
			if (obui::android::takeMenuRequest() && !s->menuOpen)
				open = true;
			if (open && !s->menuOpen && obui::android::showMenu())
			{
				s->kotlinMenu = true;
				s->menuOpen = true;
				s->trigger.setMenuOpen(true, in.time);
				std::fprintf(stderr, "[ui] menu open (Kotlin)\n");
				return 1;
			}
		}
#endif
		if (open)
		{
			if (!s->menuOpen)
			{
				// Freshly opened: the page list has the focus, on a page the in-game menu shows.
				s->level = Level::List;
				s->dialog.open = false;
				g_browser.open = false;
				if (s->page == Page::Game || s->page == Page::Cards || s->page == Page::Advanced || s->page == Page::Driver)
					s->page = Page::Controls;
				// The B / Esc / Start that may have opened it must not act at once: take their state as already seen.
				s->prevPad = in.pad;
				s->prevKeys = in.keys2;
			}
			Present(in);
			if (s->frameResult == 1)
				open = false;
			if (s->frameResult == 2)
			{
				s->trigger.setMenuOpen(false, in.time);
				return 2;
			}
		}
		if (open != s->menuOpen)
		{
			s->menuOpen = open;
			s->captureRow = -1;
			g_browser.open = false;
			s->dialog.open = false;
			s->trigger.setMenuOpen(open, in.time);
			if (!open && s->inputChanged && s->host.reloadInputBindings)
			{
				s->host.reloadInputBindings(); // the game is still held: it reads the pad again only after this frame
				s->inputChanged = false;
			}
			std::fprintf(stderr, "[ui] menu %s\n", open ? "open" : "closed");
		}
		return s->menuOpen ? 1 : 0;
	}

	uint16_t filterPad(int port, uint16_t buttons, uint8_t sticks[4], double now)
	{
		if (!s)
			return buttons;
		// The touch controls (Android overlay, core/touch_pad.h) play as part of the first pad; merged first, so the
		// menu's gate below hides them too while it is open and after it closes.
		if (port == 0)
			buttons = obui::touchPad().merge(buttons, sticks);
#if defined(__ANDROID__)
		obui::android::perfHintGameThread(); // the game thread, for Android's performance hint (once)
#endif
		if (s->trigger.menuOpen())
			for (int i = 0; i < 4; i++)
				sticks[i] = 0x80;
		return s->trigger.filterPad(buttons, now);
	}
} // namespace obui
