// file_dialog: Windows' own Browse dialogs, for the settings menu in mouse mode (a controller gets the in-app browser).
// "" when cancelled or on other platforms.
#pragma once

#include <string>

namespace obui
{
	// pattern: "*.iso" (one or several, ';'-separated). start: folder to open in ("" = the last one).
	std::string pickFile(const char* title, const char* filterName, const char* pattern, const std::string& start);
	std::string pickFolder(const char* title);
} // namespace obui
