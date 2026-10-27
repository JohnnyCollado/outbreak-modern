// file_dialog: Windows' own Browse dialogs (a disc image, a card file, a folder) for the settings menu in mouse mode.
// Blocking, on the main thread: the window does not redraw while one is open. Elsewhere they return "".
#include "file_dialog.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <string>

namespace
{
	std::wstring Wide(const std::string& s)
	{
		if (s.empty())
			return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(static_cast<size_t>(n), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
		return w;
	}

	std::string Utf8(const wchar_t* w)
	{
		if (!w || !*w)
			return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
		return s;
	}
} // namespace

namespace obui
{
	std::string pickFile(const char* title, const char* filterName, const char* pattern, const std::string& start)
	{
		wchar_t file[MAX_PATH * 4] = {};
		// "Name\0*.iso\0All files\0*.*\0\0"
		std::wstring filter = Wide(filterName ? filterName : "Files");
		filter.push_back(L'\0');
		filter += Wide(pattern ? pattern : "*.*");
		filter.push_back(L'\0');
		filter += L"All files";
		filter.push_back(L'\0');
		filter += L"*.*";
		filter.push_back(L'\0');
		filter.push_back(L'\0');
		const std::wstring wtitle = Wide(title ? title : "Choose a file");
		const std::wstring dir = Wide(start);
		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = GetActiveWindow();
		ofn.lpstrFilter = filter.c_str();
		ofn.lpstrFile = file;
		ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
		ofn.lpstrTitle = wtitle.c_str();
		ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
		return GetOpenFileNameW(&ofn) ? Utf8(file) : std::string();
	}

	std::string pickFolder(const char* title)
	{
		const std::wstring wtitle = Wide(title ? title : "Choose a folder");
		BROWSEINFOW bi = {};
		bi.hwndOwner = GetActiveWindow();
		bi.lpszTitle = wtitle.c_str();
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		std::string out;
		if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi))
		{
			wchar_t path[MAX_PATH * 4] = {};
			if (SHGetPathFromIDListW(pidl, path))
				out = Utf8(path);
			CoTaskMemFree(pidl);
		}
		if (SUCCEEDED(co))
			CoUninitialize();
		return out;
	}
} // namespace obui

#else

namespace obui
{
	std::string pickFile(const char*, const char*, const char*, const std::string&) { return {}; }
	std::string pickFolder(const char*) { return {}; }
} // namespace obui

#endif
