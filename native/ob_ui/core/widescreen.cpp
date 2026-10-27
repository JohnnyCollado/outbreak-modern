#include "widescreen.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace obui
{
	namespace
	{
		// The line without spaces/tabs/CR, lower case.
		std::string Squash(std::string_view line)
		{
			std::string out;
			out.reserve(line.size());
			for (char c : line)
				if (c != ' ' && c != '\t' && c != '\r')
					out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			return out;
		}

		bool StartsWith(const std::string& s, std::string_view p) { return s.compare(0, p.size(), p) == 0; }

		bool EndsWith(const std::string& s, std::string_view e)
		{
			return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
		}

		std::string Lower(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		std::string FileName(const fs::path& p)
		{
			const std::u8string u = p.filename().u8string();
			return std::string(u.begin(), u.end());
		}

		bool ReadText(const fs::path& p, std::string& out)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f)
				return false;
			std::stringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return true;
		}

		// x.pnach.disabled -> x.pnach and back.
		fs::path Toggled(const fs::path& p, bool enable)
		{
			const std::string name = FileName(p);
			std::string base = name;
			if (EndsWith(Lower(name), ".pnach.disabled"))
				base = name.substr(0, name.size() - 9); // drop ".disabled"
			const std::string target = enable ? base : base + ".disabled";
			return p.parent_path() / fs::u8path(target);
		}
	} // namespace

	bool isWidescreenPnach(std::string_view text)
	{
		size_t start = 0;
		while (start <= text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string_view::npos)
				end = text.size();
			const std::string line = Squash(text.substr(start, end - start));
			if (!StartsWith(line, "//") && !StartsWith(line, ";") &&
				(StartsWith(line, "patch=1,ee,20239d48,extended,3fe38e38") || StartsWith(line, "gsaspectratio=16:9")))
				return true;
			if (end == text.size())
				break;
			start = end + 1;
		}
		return false;
	}

	bool isWidescreenModFile(const fs::path& file)
	{
		const std::string low = Lower(FileName(file));
		if (!EndsWith(low, ".pnach") && !EndsWith(low, ".pnach.disabled"))
			return false;
		if (low == kOwnWidescreenFile || low == std::string(kOwnWidescreenFile) + ".disabled")
			return true;
		std::string text;
		return ReadText(file, text) && isWidescreenPnach(text);
	}

	std::vector<WidescreenMod> findWidescreenMods(const fs::path& dir)
	{
		std::vector<WidescreenMod> out;
		std::error_code ec;
		if (dir.empty() || !fs::is_directory(dir, ec))
			return out;
		for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			std::error_code e2;
			if (!it->is_regular_file(e2) || !isWidescreenModFile(it->path()))
				continue;
			const std::string low = Lower(FileName(it->path()));
			WidescreenMod m;
			m.path = it->path();
			m.enabled = EndsWith(low, ".pnach");
			m.ours = StartsWith(low, "ob-widescreen.pnach");
			out.push_back(std::move(m));
		}
		std::sort(out.begin(), out.end(),
			[](const WidescreenMod& a, const WidescreenMod& b) { return Lower(FileName(a.path)) < Lower(FileName(b.path)); });
		return out;
	}

	bool widescreenEnabled(const fs::path& dir)
	{
		for (const WidescreenMod& m : findWidescreenMods(dir))
			if (m.enabled)
				return true;
		return false;
	}

	bool setWidescreen(const fs::path& dir, bool on, std::string* err)
	{
		std::vector<WidescreenMod> mods = findWidescreenMods(dir);
		std::error_code ec;
		if (!on)
		{
			bool ok = true;
			for (const WidescreenMod& m : mods)
				if (m.enabled)
				{
					fs::rename(m.path, Toggled(m.path, false), ec);
					if (ec)
					{
						ok = false;
						if (err)
							*err = "Could not turn off " + FileName(m.path) + ": " + ec.message();
					}
				}
			return ok;
		}
		for (const WidescreenMod& m : mods)
			if (m.enabled)
				return true; // already 16:9
		// The player's own file first, then the menu's.
		std::stable_sort(mods.begin(), mods.end(), [](const WidescreenMod& a, const WidescreenMod& b) { return !a.ours && b.ours; });
		if (!mods.empty())
		{
			fs::rename(mods.front().path, Toggled(mods.front().path, true), ec);
			if (ec && err)
				*err = "Could not turn on " + FileName(mods.front().path) + ": " + ec.message();
			return !ec;
		}
		fs::create_directories(dir, ec);
		std::ofstream o(dir / kOwnWidescreenFile, std::ios::binary);
		o << "gametitle=BioHazard Outbreak [NTSC-J] (SLPM-65428) widescreen (settings menu)\n"
		     "gsaspectratio=16:9\n"
		     "patch=1,EE,20239d48,extended,3fe38e38\n";
		if (!o)
		{
			if (err)
				*err = "Could not write " + std::string(kOwnWidescreenFile) + " in " + FileName(dir);
			return false;
		}
		return true;
	}
} // namespace obui
