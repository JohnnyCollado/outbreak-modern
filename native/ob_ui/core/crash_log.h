// crash_log: the platform-free part of a release's logs and crash reports (report entry 71). A release has no console,
// so what the game prints goes to a file; a crash report is those files in one .zip with the home folder hidden.
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace obui
{
	// "outbreak-20261030-182627.log": sorts by time.
	std::string logFileName(const std::tm& when);

	// "outbreak-<time>-gs.log" for "outbreak-<time>.log": the run's own graphics log, since a shared name is overwritten
	// by the next start.
	std::string gsLogName(const std::string& logName);

	// Keeps the newest `keep` outbreak-*.log files, each with its .dmp; a .dmp without a log goes too.
	void pruneLogs(const std::filesystem::path& dir, size_t keep);

	// Stops a runaway log: bytes go to the file until `limit`, then nothing does.
	class LogCap
	{
	public:
		explicit LogCap(uint64_t limit) : m_left(limit) {}
		size_t accept(size_t bytes)
		{
			const size_t n = bytes < m_left ? bytes : static_cast<size_t>(m_left);
			m_left -= n;
			return n;
		}

	private:
		uint64_t m_left;
	};

	// Overwrites every copy of `home` (any case, either slash), in ASCII and in UTF-16, with "~___": the size stays, so
	// this is safe for a binary dump as well as for a log.
	void maskHome(std::string& bytes, const std::string& home);

	// A .zip of stored (uncompressed) files, in order. Names are used as given.
	std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files);

	// Writes `out`: every outbreak-* file of logDir plus `extras`, home masked. A file over 64 MiB or a net.env is left
	// out. False with `error` set when there is nothing to send or `out` cannot be written.
	bool writeCrashReport(const std::filesystem::path& logDir, const std::vector<std::filesystem::path>& extras,
		const std::string& home, const std::filesystem::path& out, std::string& error);
} // namespace obui
