// disc_check: the launcher's three disc checks (docs/plans/2026-10-26-settings-menu.md section 3). Play is enabled
// only when all three pass: 1. the file reads as an ISO 9660 disc image; 2. its SYSTEM.CNF boots SLPM_654.28;
// 3. that boot ELF is the one this build was recompiled from (SHA-256; the PCSX2 CRC names a known other release).
// Reads only the volume descriptor, the root directory, SYSTEM.CNF and the ELF (1.3 MB), through std::FILE seeks
// and reads, so an Android descriptor path (/proc/self/fd/N) works as well as a file name.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obui
{
	struct ExpectedElf
	{
		std::string serial; // SLPM-65428
		uint64_t size = 0;
		std::string sha256; // lower-case hex
		uint32_t crc = 0;   // PCSX2's ELF CRC (XOR of the little-endian 32-bit words)
	};
	// The ELF this build is made from: game_files/SLPM_654.28 (English patch v2.1), measured 2026-10-26. A build from
	// another disc passes OB_UI_ELF_SHA256 / OB_UI_ELF_SIZE / OB_UI_ELF_CRC.
	ExpectedElf builtInExpectedElf();

	enum class DiscVerdict
	{
		Ok,
		NoFile,
		NotDiscImage,
		NoSystemCnf,
		WrongGame,
		WrongRelease,
		ReadError,
	};
	struct DiscReport
	{
		DiscVerdict verdict = DiscVerdict::NoFile;
		int stepsPassed = 0; // 0..3
		std::string serial, bootFile, version, sha256;
		uint32_t crc = 0;
		std::string message; // what the launcher shows under the disc field
	};
	// path: a .iso (the launcher), or (outbreak.ini / command line) an extracted folder or the boot ELF itself.
	DiscReport checkDisc(const std::filesystem::path& path, const ExpectedElf& expected);

	// Remembers the last verdict for a file; the launcher skips the check while path, size and time are unchanged.
	struct DiscCache
	{
		std::filesystem::path path;
		uint64_t size = 0;
		int64_t mtime = 0;
		DiscReport report;
		bool valid = false;

		bool sameFileAs(const std::filesystem::path& p) const;
		void remember(const std::filesystem::path& p, const DiscReport& r);
	};

	// Exposed for the tests.
	std::string sha256Hex(const uint8_t* data, size_t size);
	uint32_t pcsx2ElfCrc(const uint8_t* data, size_t size);
	// "BOOT2 = cdrom0:\SLPM_654.28;1" -> boot file "SLPM_654.28", serial "SLPM-65428"; VER -> version.
	bool parseSystemCnf(const std::string& text, std::string& bootFile, std::string& serial, std::string& version);
} // namespace obui
