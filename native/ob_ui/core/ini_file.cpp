// ini_file: see ini_file.h.
#include "ini_file.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace obui
{
	std::string toLower(std::string_view s)
	{
		std::string out(s);
		for (char& c : out)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	std::string trim(std::string_view s)
	{
		size_t b = 0, e = s.size();
		while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
			++b;
		while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
			--e;
		return std::string(s.substr(b, e - b));
	}

	namespace
	{
		bool isKeyChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'; }

		// "name = ..." at pos: the key name (lower case) and the position just after '='; empty name = not a key line.
		std::string keyAt(const std::string& line, size_t pos, size_t& afterEq)
		{
			size_t e = pos;
			while (e < line.size() && isKeyChar(line[e]))
				++e;
			if (e == pos)
				return {};
			size_t q = e;
			while (q < line.size() && (line[q] == ' ' || line[q] == '\t'))
				++q;
			if (q >= line.size() || line[q] != '=')
				return {};
			afterEq = q + 1;
			return toLower(std::string_view(line).substr(pos, e - pos));
		}
	} // namespace

	IniFile::IniFile(bool inlineComments) : m_inlineComments(inlineComments) {}

	void IniFile::parse(std::string_view text)
	{
		m_lines.clear();
		size_t pos = 0;
		while (pos < text.size())
		{
			const size_t nl = text.find('\n', pos);
			Line l;
			if (nl == std::string_view::npos)
			{
				l.text = std::string(text.substr(pos));
				pos = text.size();
			}
			else
			{
				size_t end = nl;
				l.eol = "\n";
				if (end > pos && text[end - 1] == '\r')
				{
					--end;
					l.eol = "\r\n";
				}
				l.text = std::string(text.substr(pos, end - pos));
				pos = nl + 1;
			}
			m_lines.push_back(std::move(l));
		}
	}

	bool IniFile::load(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
		{
			m_lines.clear();
			return !ec;
		}
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		std::ostringstream ss;
		ss << f.rdbuf();
		parse(ss.str());
		return true;
	}

	bool IniFile::save(const std::filesystem::path& path) const
	{
		std::filesystem::path tmp = path;
		tmp += ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if (!f)
				return false;
			const std::string t = text();
			f.write(t.data(), static_cast<std::streamsize>(t.size()));
			if (!f)
				return false;
		}
		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			std::filesystem::remove(tmp, ec);
			return false;
		}
		return true;
	}

	std::string IniFile::text() const
	{
		std::string out;
		for (const Line& l : m_lines)
		{
			out += l.text;
			out += l.eol;
		}
		return out;
	}

	IniFile::Parsed IniFile::classify(const std::string& line) const
	{
		Parsed p;
		size_t b = 0;
		while (b < line.size() && (line[b] == ' ' || line[b] == '\t'))
			++b;
		if (b >= line.size())
			return p;
		if (line[b] == '[')
		{
			const size_t close = line.find(']', b + 1);
			if (close != std::string::npos)
			{
				p.kind = Parsed::Section;
				p.name = toLower(trim(std::string_view(line).substr(b + 1, close - b - 1)));
			}
			return p;
		}
		if (line[b] == ';' || line[b] == '#')
		{
			// A commented example of a key ("; upscale = 3"): where a new active line of that key goes.
			size_t c = b;
			while (c < line.size() && (line[c] == ';' || line[c] == '#' || line[c] == ' ' || line[c] == '\t'))
				++c;
			size_t afterEq = 0;
			p.name = keyAt(line, c, afterEq);
			if (!p.name.empty())
				p.kind = Parsed::CommentedKey;
			return p;
		}
		size_t afterEq = 0;
		p.name = keyAt(line, b, afterEq);
		if (p.name.empty())
			return p;
		p.kind = Parsed::Key;
		size_t vb = afterEq;
		while (vb < line.size() && (line[vb] == ' ' || line[vb] == '\t'))
			++vb;
		size_t ve = line.size();
		if (m_inlineComments)
		{
			size_t scan = vb;
			if (scan < line.size() && line[scan] == '"')
			{
				const size_t q = line.find('"', scan + 1);
				scan = q == std::string::npos ? line.size() : q + 1;
			}
			const size_t c = line.find_first_of(";#", scan);
			if (c != std::string::npos)
				ve = c;
		}
		while (ve > vb && (line[ve - 1] == ' ' || line[ve - 1] == '\t'))
			--ve;
		p.valueBegin = vb;
		p.valueEnd = ve;
		return p;
	}

	std::string IniFile::newline() const
	{
		for (const Line& l : m_lines)
			if (!l.eol.empty())
				return l.eol;
		return "\n";
	}

	bool IniFile::sectionRange(std::string_view section, size_t& first, size_t& last) const
	{
		const std::string want = toLower(section);
		bool in = want.empty();
		bool found = in;
		first = 0;
		last = m_lines.size();
		for (size_t i = 0; i < m_lines.size(); ++i)
		{
			const Parsed p = classify(m_lines[i].text);
			if (p.kind != Parsed::Section)
				continue;
			if (in)
			{
				last = i;
				return true;
			}
			if (p.name == want)
			{
				in = found = true;
				first = i + 1;
			}
		}
		return found;
	}

	std::optional<std::string> IniFile::get(std::string_view section, std::string_view key) const
	{
		size_t first = 0, last = 0;
		if (!sectionRange(section, first, last))
			return std::nullopt;
		const std::string want = toLower(key);
		std::optional<std::string> value;
		for (size_t i = first; i < last; ++i)
		{
			const Parsed p = classify(m_lines[i].text);
			if (p.kind != Parsed::Key || p.name != want)
				continue;
			std::string v = m_lines[i].text.substr(p.valueBegin, p.valueEnd - p.valueBegin);
			if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
				v = v.substr(1, v.size() - 2);
			value = v;
		}
		return value;
	}

	void IniFile::set(std::string_view section, std::string_view key, std::string_view value)
	{
		std::string v(value);
		// One setting stays one line, whatever the caller passes (security queue 2, F3).
		v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\r' || c == '\n'; }), v.end());
		const bool needsQuotes = !v.empty() && (std::isspace(static_cast<unsigned char>(v.front())) ||
			std::isspace(static_cast<unsigned char>(v.back())) || v.front() == '"' ||
			(m_inlineComments && v.find_first_of(";#") != std::string::npos));
		if (needsQuotes)
			v = "\"" + v + "\"";

		size_t first = 0, last = 0;
		const std::string want = toLower(key);
		if (sectionRange(section, first, last))
		{
			size_t lastKey = std::string::npos, lastExample = std::string::npos, lastContent = std::string::npos;
			for (size_t i = first; i < last; ++i)
			{
				const Parsed p = classify(m_lines[i].text);
				if (p.kind == Parsed::Key && p.name == want)
					lastKey = i;
				else if (p.kind == Parsed::CommentedKey && p.name == want)
					lastExample = i;
				if (!trim(m_lines[i].text).empty())
					lastContent = i;
			}
			if (lastKey != std::string::npos)
			{
				std::string& line = m_lines[lastKey].text;
				const Parsed p = classify(line);
				size_t replaceEnd = p.valueEnd;
				std::string repl = v;
				if (p.valueEnd < line.size())
				{
					// An inline comment follows: keep its column when the new value fits, else one space before it.
					size_t commentPos = p.valueEnd;
					while (commentPos < line.size() && (line[commentPos] == ' ' || line[commentPos] == '\t'))
						++commentPos;
					replaceEnd = commentPos;
					const size_t width = commentPos - p.valueBegin;
					repl = v.size() + 1 <= width ? v + std::string(width - v.size(), ' ') : v + " ";
				}
				line.replace(p.valueBegin, replaceEnd - p.valueBegin, repl);
				return;
			}
			size_t at = lastExample != std::string::npos ? lastExample + 1
				: lastContent != std::string::npos ? lastContent + 1 : first;
			if (at > 0 && m_lines[at - 1].eol.empty())
				m_lines[at - 1].eol = newline();
			const Line l{std::string(key) + " = " + v, newline()};
			m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(at), l);
			return;
		}
		// A new section at the end of the file.
		if (!m_lines.empty())
		{
			if (m_lines.back().eol.empty())
				m_lines.back().eol = newline();
			if (!trim(m_lines.back().text).empty())
				m_lines.push_back(Line{"", newline()});
		}
		m_lines.push_back(Line{"[" + std::string(section) + "]", newline()});
		m_lines.push_back(Line{std::string(key) + " = " + v, newline()});
	}

	bool IniFile::remove(std::string_view section, std::string_view key)
	{
		size_t first = 0, last = 0;
		if (!sectionRange(section, first, last))
			return false;
		const std::string want = toLower(key);
		bool removed = false;
		for (size_t i = last; i-- > first;)
		{
			const Parsed p = classify(m_lines[i].text);
			if (p.kind == Parsed::Key && p.name == want)
			{
				m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(i));
				removed = true;
			}
		}
		return removed;
	}
} // namespace obui
