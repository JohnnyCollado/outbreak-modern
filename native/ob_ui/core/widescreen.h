// widescreen: the Display page's Aspect ratio switch over the mods folder (developer, 2026-10-26: "treat any pnach
// containing the widescreen patch as the hidden widescreen mod tied to Aspect ratio"). A widescreen mod is any .pnach /
// .pnach.disabled in <mods>/<serial>/ carrying the gameplay widescreen patch (PCSX2's SLPM-65428_32088394.pnach,
// "gameplay by synce") or gsaspectratio=16:9, plus the menu's own ob-widescreen.pnach. The Mods page never lists them.
// On / off is a rename (x.pnach <-> x.pnach.disabled): the runner loads only ".pnach" (PS2Recomp ps2_pnach.cpp:221),
// and the player's own files are never deleted. Used by the Windows menu and, through JNI, the Android launcher.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace obui
{
	// The menu's own file, written when 16:9 is chosen and no widescreen mod exists.
	constexpr const char* kOwnWidescreenFile = "ob-widescreen.pnach";

	// True when the text has, on a line of its own (case and spaces ignored, "//" or ";" comment lines skipped),
	// `patch=1,EE,20239d48,extended,3fe38e38` or `gsaspectratio=16:9`.
	bool isWidescreenPnach(std::string_view text);

	struct WidescreenMod
	{
		std::filesystem::path path;
		bool enabled = false; // .pnach (else .pnach.disabled)
		bool ours = false;    // ob-widescreen.pnach
	};
	// The widescreen mods in dir (<mods>/<serial>/), sorted by file name; empty when dir is missing.
	std::vector<WidescreenMod> findWidescreenMods(const std::filesystem::path& dir);
	// True when the file (a .pnach or .pnach.disabled) is one of them: the menu's own name, or its text matches.
	bool isWidescreenModFile(const std::filesystem::path& file);
	// The Aspect ratio shown: 16:9 when any widescreen mod is enabled.
	bool widescreenEnabled(const std::filesystem::path& dir);
	// 16:9: enables one disabled widescreen mod (the player's own before ob-widescreen.pnach), or writes
	// ob-widescreen.pnach when there is none (dir created). 4:3: renames every enabled one to .pnach.disabled.
	// Nothing is deleted. False with err when a rename or the write failed.
	bool setWidescreen(const std::filesystem::path& dir, bool on, std::string* err);
} // namespace obui
