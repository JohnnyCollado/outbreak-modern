#include "crash_log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace obui
{
	namespace
	{
		constexpr uintmax_t kMaxReportFile = 64u * 1024u * 1024u;

		std::string FileName(const fs::path& p)
		{
			const auto name = p.filename().u8string(); // char8_t in C++20
			return std::string(name.begin(), name.end());
		}

		bool IsLogFile(const std::string& name, const char* extension)
		{
			return name.rfind("outbreak-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, extension) == 0;
		}

		bool IsGsLog(const std::string& name)
		{
			return IsLogFile(name, ".log") && name.size() > 7 && name.compare(name.size() - 7, 7, "-gs.log") == 0;
		}

		char Fold(char c)
		{
			return c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		// `step` bytes per character of `home`: 1 ASCII, 2 UTF-16 (little-endian, the high byte 0).
		void MaskOne(std::string& bytes, const std::string& home, size_t step)
		{
			const size_t span = home.size() * step;
			if (bytes.size() < span)
				return;
			for (size_t at = 0; at + span <= bytes.size(); ++at)
			{
				bool match = true;
				for (size_t i = 0; i < home.size() && match; ++i)
					match = Fold(bytes[at + i * step]) == Fold(home[i]) && (step == 1 || bytes[at + i * step + 1] == 0);
				if (!match)
					continue;
				for (size_t i = 0; i < home.size(); ++i)
					bytes[at + i * step] = i == 0 ? '~' : '_';
				at += span - 1;
			}
		}

		uint32_t Crc32(const std::string& data)
		{
			static const std::array<uint32_t, 256> table = [] {
				std::array<uint32_t, 256> t{};
				for (uint32_t i = 0; i < 256u; ++i)
				{
					uint32_t c = i;
					for (int k = 0; k < 8; ++k)
						c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					t[i] = c;
				}
				return t;
			}();
			uint32_t c = 0xFFFFFFFFu;
			for (const char b : data)
				c = table[(c ^ static_cast<unsigned char>(b)) & 0xFFu] ^ (c >> 8);
			return c ^ 0xFFFFFFFFu;
		}

		void Put16(std::string& out, uint32_t v)
		{
			out.push_back(static_cast<char>(v & 0xFFu));
			out.push_back(static_cast<char>((v >> 8) & 0xFFu));
		}

		void Put32(std::string& out, uint32_t v)
		{
			Put16(out, v & 0xFFFFu);
			Put16(out, v >> 16);
		}

		// The fields a local header and a directory entry share, from "version needed" to the name's length.
		void PutCommon(std::string& out, uint32_t crc, uint32_t size, uint32_t nameSize)
		{
			Put16(out, 20u); // version needed
			Put16(out, 0u);  // flags
			Put16(out, 0u);  // stored
			Put16(out, 0u);  // time
			Put16(out, 0x21u); // date: 1980-01-01
			Put32(out, crc);
			Put32(out, size);
			Put32(out, size);
			Put16(out, nameSize);
			Put16(out, 0u); // extra field
		}
	} // namespace

	std::string logFileName(const std::tm& when)
	{
		char name[64];
		std::snprintf(name, sizeof(name), "outbreak-%04d%02d%02d-%02d%02d%02d.log", when.tm_year + 1900, when.tm_mon + 1,
			when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);
		return name;
	}

	std::string gsLogName(const std::string& logName)
	{
		const size_t dot = logName.rfind('.');
		return logName.substr(0, dot) + "-gs" + (dot == std::string::npos ? std::string() : logName.substr(dot));
	}

	void pruneLogs(const fs::path& dir, size_t keep)
	{
		std::vector<std::string> logs;
		std::vector<std::string> dumps;
		std::vector<std::string> gsLogs;
		std::error_code ec;
		for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			const std::string name = FileName(it->path());
			if (IsGsLog(name))
				gsLogs.push_back(name);
			else if (IsLogFile(name, ".log"))
				logs.push_back(name);
			else if (IsLogFile(name, ".dmp"))
				dumps.push_back(name);
		}
		std::sort(logs.begin(), logs.end());
		std::set<std::string> kept;
		for (size_t i = 0; i < logs.size(); ++i)
		{
			if (logs.size() - i > keep)
				fs::remove(dir / fs::u8path(logs[i]), ec);
			else
				kept.insert(logs[i].substr(0, logs[i].size() - 4));
		}
		for (const std::string& d : dumps)
			if (kept.count(d.substr(0, d.size() - 4)) == 0)
				fs::remove(dir / fs::u8path(d), ec);
		for (const std::string& g : gsLogs)
			if (kept.count(g.substr(0, g.size() - 7)) == 0)
				fs::remove(dir / fs::u8path(g), ec);
	}

	void maskHome(std::string& bytes, const std::string& home)
	{
		if (home.empty())
			return;
		MaskOne(bytes, home, 1);
		MaskOne(bytes, home, 2);
	}

	std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files)
	{
		std::string out;
		std::string directory;
		for (const auto& [name, data] : files)
		{
			const uint32_t crc = Crc32(data);
			const uint32_t offset = static_cast<uint32_t>(out.size());
			Put32(out, 0x04034b50u);
			PutCommon(out, crc, static_cast<uint32_t>(data.size()), static_cast<uint32_t>(name.size()));
			out += name;
			out += data;

			Put32(directory, 0x02014b50u);
			Put16(directory, 20u); // version made by
			PutCommon(directory, crc, static_cast<uint32_t>(data.size()), static_cast<uint32_t>(name.size()));
			Put16(directory, 0u); // comment
			Put16(directory, 0u); // disk
			Put16(directory, 0u); // internal attributes
			Put32(directory, 0u); // external attributes
			Put32(directory, offset);
			directory += name;
		}
		const uint32_t directoryAt = static_cast<uint32_t>(out.size());
		out += directory;
		Put32(out, 0x06054b50u);
		Put16(out, 0u);
		Put16(out, 0u);
		Put16(out, static_cast<uint32_t>(files.size()));
		Put16(out, static_cast<uint32_t>(files.size()));
		Put32(out, static_cast<uint32_t>(directory.size()));
		Put32(out, directoryAt);
		Put16(out, 0u);
		return out;
	}

	bool writeCrashReport(const fs::path& logDir, const std::vector<fs::path>& extras, const std::string& home,
		const fs::path& out, std::string& error)
	{
		std::vector<fs::path> paths;
		std::error_code ec;
		for (fs::directory_iterator it(logDir, ec), end; !ec && it != end; it.increment(ec))
		{
			const std::string name = FileName(it->path());
			if (IsLogFile(name, ".log") || IsLogFile(name, ".dmp"))
				paths.push_back(it->path());
		}
		std::sort(paths.begin(), paths.end());
		paths.insert(paths.end(), extras.begin(), extras.end());

		std::vector<std::pair<std::string, std::string>> files;
		for (const fs::path& p : paths)
		{
			const std::string name = FileName(p);
			std::error_code sizeError;
			const uintmax_t size = fs::file_size(p, sizeError);
			if (name == "net.env" || sizeError || size > kMaxReportFile)
				continue;
			std::ifstream f(p, std::ios::binary);
			if (!f)
				continue;
			std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			maskHome(data, home);
			files.emplace_back(name, std::move(data));
		}
		if (files.empty())
		{
			error = "There are no logs to send yet";
			return false;
		}
		std::ofstream f(out, std::ios::binary);
		const std::string zip = zipStored(files);
		f.write(zip.data(), static_cast<std::streamsize>(zip.size()));
		f.close();
		if (!f)
		{
			error = "Could not write the report";
			return false;
		}
		return true;
	}
} // namespace obui
