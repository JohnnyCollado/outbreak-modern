// android_picker: Android's file picker (Storage Access Framework) for the settings menu, through the APK's one Java
// class (native/android/java/com/outbreakmodern/app/OutbreakActivity.java) over JNI
// (docs/plans/2026-10-26-settings-menu.md section 5.8). Java picks and opens the document and hands back a file
// descriptor; C++ reads it as an ordinary file. Android only. Log lines go to stderr ("[ui] ..."), which the runner
// redirects to logcat (tag ps2x), as the GS adapter does.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace obui::android
{
	// The request codes of OutbreakActivity (PICK_*).
	enum class PickKind : int
	{
		Disc = 1,        // .iso; the URI's read permission is kept across restarts (openDocument)
		DriverZip = 2,   // a GPU driver package (.zip)
		TexturesZip = 3, // a texture pack (.zip), imported into the app's files folder
		ModsZip = 4,     // mods (.zip), imported likewise
	};

	struct PickResult
	{
		PickKind kind = PickKind::Disc;
		int fd = -1;             // owned by the caller (close it); -1 = cancelled or not opened
		std::string displayName; // the document's name as the provider shows it; may be empty
		std::string uri;         // content:// URI; for a disc, save it and reopen it later with openDocument
		bool cancelled = true;
	};

	// On the thread that runs main (raylib's android_native_app_glue): finds the activity, registers nativePicked.
	// False (one "[ui] android picker unavailable: ..." line) when the APK has no OutbreakActivity.pickDocument
	// (an older APK); the other calls then do nothing and return failure.
	bool init();
	// Starts the picker (asynchronous: the answer arrives through pollPick). False when init() failed.
	bool startPick(PickKind kind);
	// Takes the oldest answer, if any. Thread-safe.
	bool pollPick(PickResult& out);
	// A fresh read-only descriptor for a URI picked earlier (persisted permission); -1 on failure. Any thread.
	int openDocument(const std::string& uri);
	// "/proc/self/fd/<fd>": a path that reopens the file behind fd (works for a real file; a cloud document may be a
	// pipe, which cannot seek: check with isSeekableFile).
	std::string fdPath(int fd);
	// True when fd is a regular file (so the ISO reader, which seeks, can use fdPath(fd)).
	bool isSeekableFile(int fd);
	// Copies everything behind fd (from offset 0, pread) to dest through dest + ".tmp", then renames. Leaves fd open.
	bool copyFdToFile(int fd, const std::filesystem::path& dest, std::string* err);
	// Extracts zip into destDir with libzip: entries whose name keep() accepts (null = all), never one with ".." or an
	// absolute path (refused, err says which). Directories are created as needed.
	bool extractZip(const std::filesystem::path& zip, const std::filesystem::path& destDir,
		const std::function<bool(const std::string& entryName)>& keep, std::string* err);
} // namespace obui::android
