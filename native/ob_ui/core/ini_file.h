// ini_file: a key = value file edited in place (outbreak.ini, input.ini). Every line the menu does not change stays
// byte-identical (comments, blank lines, unknown keys, CRLF or LF), so a hand-edited file survives the menu.
// Reading follows the runtime's parsers (PS2Recomp patches 0038, 0043): keys and sections case-insensitive, a line
// starting with ';' or '#' is a comment, a value may be in double quotes, the last active line of a key wins.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obui
{
	class IniFile
	{
	public:
		// inlineComments: input.ini's "key = value ; comment" (get strips it, set keeps it). Off for outbreak.ini,
		// where a ';' inside a value belongs to it (paths).
		explicit IniFile(bool inlineComments = false);

		void parse(std::string_view text);
		// A missing file reads as empty and returns true; false only when the file exists and cannot be read.
		bool load(const std::filesystem::path& path);
		// Writes <path>.tmp, then renames it over path.
		bool save(const std::filesystem::path& path) const;
		std::string text() const;

		// section "" = the lines before any [section].
		std::optional<std::string> get(std::string_view section, std::string_view key) const;
		// Replaces the value of the key's last active line; else adds "key = value" after the key's commented example
		// ("; key = ...") when the section has one, else at the end of the section (a new [section] at the file end).
		void set(std::string_view section, std::string_view key, std::string_view value);
		// Removes every active line of the key; false when there was none.
		bool remove(std::string_view section, std::string_view key);

	private:
		struct Line
		{
			std::string text; // without the line ending
			std::string eol;  // "\r\n", "\n" or "" (last line without one)
		};
		struct Parsed
		{
			enum Kind { Other, Section, Key, CommentedKey } kind = Other;
			std::string name; // section or key, lower case
			size_t valueBegin = 0, valueEnd = 0; // value span in the line (Key), before any inline comment
		};

		Parsed classify(const std::string& line) const;
		std::string newline() const;
		// [first, last) line indexes of the section's body ("" = file start up to the first header). npos = absent.
		bool sectionRange(std::string_view section, size_t& first, size_t& last) const;

		bool m_inlineComments;
		std::vector<Line> m_lines;
	};

	std::string toLower(std::string_view s);
	std::string trim(std::string_view s);
} // namespace obui
