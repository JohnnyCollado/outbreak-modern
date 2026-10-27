// disc_check: see disc_check.h. ISO 9660 layout: ECMA-119 (primary volume descriptor at sector 16, root directory
// record at offset 156, directory records: length, extent LBA at 2, data length at 10, flags at 25, name length at
// 32, name at 33), the same subset PS2Recomp patch 0043's reader uses.
#include "disc_check.h"

#include "ini_file.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef OB_UI_ELF_SHA256
#define OB_UI_ELF_SHA256 "975dd77d1e1c81f0d8f5f072b35c487c5ad6befbb99b339641a4c9573a334480"
#endif
#ifndef OB_UI_ELF_SIZE
#define OB_UI_ELF_SIZE 1294104
#endif
#ifndef OB_UI_ELF_CRC
#define OB_UI_ELF_CRC 0x32088394
#endif

namespace obui
{
	ExpectedElf builtInExpectedElf()
	{
		return ExpectedElf{"SLPM-65428", static_cast<uint64_t>(OB_UI_ELF_SIZE), OB_UI_ELF_SHA256,
			static_cast<uint32_t>(OB_UI_ELF_CRC)};
	}

	// ---- SHA-256 (FIPS 180-4) ----
	namespace
	{
		constexpr uint32_t kK[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
			0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
			0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138,
			0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70,
			0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
			0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa,
			0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

		inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

		void Block(uint32_t h[8], const uint8_t* p)
		{
			uint32_t w[64];
			for (int i = 0; i < 16; ++i)
				w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
			for (int i = 16; i < 64; ++i)
			{
				const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
			for (int i = 0; i < 64; ++i)
			{
				const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
				const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				hh = g;
				g = f;
				f = e;
				e = d + t1;
				d = c;
				c = b;
				b = a;
				a = t1 + t2;
			}
			h[0] += a;
			h[1] += b;
			h[2] += c;
			h[3] += d;
			h[4] += e;
			h[5] += f;
			h[6] += g;
			h[7] += hh;
		}
	} // namespace

	std::string sha256Hex(const uint8_t* data, size_t size)
	{
		uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		size_t i = 0;
		for (; i + 64 <= size; i += 64)
			Block(h, data + i);
		uint8_t tail[128] = {};
		const size_t rest = size - i;
		if (rest)
			std::memcpy(tail, data + i, rest);
		tail[rest] = 0x80;
		const size_t tailLen = rest + 9 <= 64 ? 64 : 128;
		const uint64_t bits = static_cast<uint64_t>(size) * 8u;
		for (int b = 0; b < 8; ++b)
			tail[tailLen - 1 - b] = static_cast<uint8_t>(bits >> (8 * b));
		Block(h, tail);
		if (tailLen == 128)
			Block(h, tail + 64);
		static const char* hex = "0123456789abcdef";
		std::string out;
		out.reserve(64);
		for (uint32_t v : h)
			for (int s = 28; s >= 0; s -= 4)
				out += hex[(v >> s) & 0xF];
		return out;
	}

	uint32_t pcsx2ElfCrc(const uint8_t* data, size_t size)
	{
		uint32_t crc = 0;
		for (size_t i = 0; i + 4 <= size; i += 4)
			crc ^= uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) | (uint32_t(data[i + 2]) << 16) | (uint32_t(data[i + 3]) << 24);
		return crc;
	}

	bool parseSystemCnf(const std::string& text, std::string& bootFile, std::string& serial, std::string& version)
	{
		IniFile ini;
		ini.parse(text);
		const std::optional<std::string> boot = ini.get("", "BOOT2");
		if (const std::optional<std::string> ver = ini.get("", "VER"))
			version = *ver;
		if (!boot)
			return false;
		std::string b = *boot;
		const size_t colon = b.find(':');
		if (colon != std::string::npos)
			b = b.substr(colon + 1);
		while (!b.empty() && (b.front() == '\\' || b.front() == '/'))
			b.erase(b.begin());
		const size_t semi = b.find(';');
		if (semi != std::string::npos)
			b = b.substr(0, semi);
		const size_t slash = b.find_last_of("\\/");
		if (slash != std::string::npos)
			b = b.substr(slash + 1);
		if (b.empty())
			return false;
		bootFile = b;
		serial.clear();
		for (char c : b)
		{
			if (c == '_')
				serial += '-';
			else if (c != '.')
				serial += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		return true;
	}

	// ---- ISO 9660 ----
	namespace
	{
		constexpr uint32_t kSector = 2048;

		struct File
		{
			std::FILE* f = nullptr;
			~File()
			{
				if (f)
					std::fclose(f);
			}
		};

		// "/proc/self/fd/<n>": a descriptor the app already holds (Android: a disc picked through the system picker). It is
		// used through a duplicate, never reopened by that name: reopening re-checks access to the file's real location,
		// which a picker-granted app does not have (Android 16 on a OnePlus: "This file can't be opened"). Reads always
		// seek first (ReadAt), so sharing the descriptor's offset is harmless.
		int FdPath(const std::filesystem::path& p)
		{
			const std::string s = p.generic_string();
			constexpr std::string_view kPrefix = "/proc/self/fd/";
			if (s.size() <= kPrefix.size() || s.compare(0, kPrefix.size(), kPrefix) != 0)
				return -1;
			int n = 0;
			for (size_t i = kPrefix.size(); i < s.size(); i++)
			{
				if (s[i] < '0' || s[i] > '9' || n > 100000)
					return -1;
				n = n * 10 + (s[i] - '0');
			}
			return n;
		}

		std::FILE* OpenRead(const std::filesystem::path& p)
		{
			if (const int fd = FdPath(p); fd >= 0)
			{
#if defined(_WIN32)
				const int d = ::_dup(fd);
				std::FILE* f = d >= 0 ? ::_fdopen(d, "rb") : nullptr;
				if (!f && d >= 0)
					::_close(d);
#else
				const int d = ::dup(fd);
				std::FILE* f = d >= 0 ? ::fdopen(d, "rb") : nullptr;
				if (!f && d >= 0)
					::close(d);
#endif
				return f;
			}
#if defined(_WIN32)
			return ::_wfopen(p.c_str(), L"rb");
#else
			return std::fopen(p.c_str(), "rb");
#endif
		}

		bool ReadAt(std::FILE* f, uint64_t offset, uint8_t* dst, size_t n)
		{
#if defined(_WIN32)
			if (::_fseeki64(f, static_cast<long long>(offset), SEEK_SET) != 0)
				return false;
#else
			if (::fseeko(f, static_cast<off_t>(offset), SEEK_SET) != 0)
				return false;
#endif
			return std::fread(dst, 1, n, f) == n;
		}

		uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

		std::string Upper(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			return s;
		}

		// One file in the root directory by name (case-insensitive, ";1" ignored): its LBA and size.
		bool FindInRoot(std::FILE* f, uint32_t rootLba, uint32_t rootSize, const std::string& name, uint32_t& lba, uint32_t& size)
		{
			const std::string want = Upper(name);
			std::vector<uint8_t> dir(rootSize);
			if (rootSize == 0 || rootSize > 64u * kSector || !ReadAt(f, uint64_t(rootLba) * kSector, dir.data(), rootSize))
				return false;
			size_t pos = 0;
			while (pos < dir.size())
			{
				const uint8_t len = dir[pos];
				if (len == 0)
				{
					pos = (pos / kSector + 1) * kSector; // records never cross a sector: zero padding to the next one
					continue;
				}
				if (pos + len > dir.size() || len < 34)
					return false;
				const uint8_t nameLen = dir[pos + 32];
				if (33u + nameLen <= len)
				{
					std::string n(reinterpret_cast<const char*>(&dir[pos + 33]), nameLen);
					const size_t semi = n.find(';');
					if (semi != std::string::npos)
						n = n.substr(0, semi);
					if (!n.empty() && n.back() == '.')
						n.pop_back();
					if (Upper(n) == want && !(dir[pos + 25] & 2))
					{
						lba = Le32(&dir[pos + 2]);
						size = Le32(&dir[pos + 10]);
						return true;
					}
				}
				pos += len;
			}
			return false;
		}

		bool ReadWholeFile(const std::filesystem::path& p, std::vector<uint8_t>& out)
		{
			File f{OpenRead(p)};
			if (!f.f)
				return false;
			out.clear();
			uint8_t buf[65536];
			size_t n;
			while ((n = std::fread(buf, 1, sizeof(buf), f.f)) > 0)
				out.insert(out.end(), buf, buf + n);
			return !std::ferror(f.f);
		}

		const char* KnownRelease(uint32_t crc)
		{
			switch (crc)
			{
				case 0x03C8F393u: return "the original Japanese disc, without the English patch";
				case 0x198EFDC1u: return "a different English patch release; this build needs v2.1";
				default: return "an unknown version";
			}
		}

		// Steps 2 and 3 on the SYSTEM.CNF text and the boot ELF bytes.
		void Judge(DiscReport& r, const std::string& cnf, const std::vector<uint8_t>& elf, bool elfRead, const ExpectedElf& expected)
		{
			if (!parseSystemCnf(cnf, r.bootFile, r.serial, r.version))
			{
				r.verdict = DiscVerdict::NoSystemCnf;
				r.message = "This disc has no PS2 boot file (SYSTEM.CNF). Choose Resident Evil Outbreak File #1.";
				return;
			}
			if (r.serial != expected.serial)
			{
				r.verdict = DiscVerdict::WrongGame;
				r.message = "This disc is a different game (" + r.serial + "). Choose Resident Evil Outbreak File #1 (" +
					expected.serial + ").";
				return;
			}
			r.stepsPassed = 2;
			if (!elfRead)
			{
				r.verdict = DiscVerdict::ReadError;
				r.message = "The game's program (" + r.bootFile + ") can't be read from this disc. Dump or copy it again.";
				return;
			}
			r.sha256 = sha256Hex(elf.data(), elf.size());
			r.crc = pcsx2ElfCrc(elf.data(), elf.size());
			if (elf.size() != expected.size || r.sha256 != expected.sha256)
			{
				r.verdict = DiscVerdict::WrongRelease;
				char crc[16];
				std::snprintf(crc, sizeof(crc), "%08X", r.crc);
				r.message = std::string("This copy of Outbreak is ") + KnownRelease(r.crc) + " (CRC " + crc +
					"). This build needs the English patch v2.1 disc.";
				return;
			}
			r.stepsPassed = 3;
			r.verdict = DiscVerdict::Ok;
			r.message = "Verified.";
		}
	} // namespace

	DiscReport checkDisc(const std::filesystem::path& path, const ExpectedElf& expected)
	{
		DiscReport r;
		std::error_code ec;
		const bool held = FdPath(path) >= 0; // a held descriptor: always a file; opening it says whether it is valid
		if (path.empty() || (!held && !std::filesystem::exists(path, ec)))
		{
			r.verdict = DiscVerdict::NoFile;
			r.message = path.empty() ? "Choose your game disc to play." : "This file can't be found. Choose your disc again.";
			return r;
		}
		if (!held && std::filesystem::is_directory(path, ec))
		{
			// An extracted folder (outbreak.ini / command line only): SYSTEM.CNF and the boot file in it.
			r.stepsPassed = 1;
			std::vector<uint8_t> cnf, elf;
			if (!ReadWholeFile(path / "SYSTEM.CNF", cnf))
			{
				r.verdict = DiscVerdict::NoSystemCnf;
				r.message = "This folder has no PS2 boot file (SYSTEM.CNF).";
				return r;
			}
			const std::string cnfText(cnf.begin(), cnf.end());
			std::string boot, serial, ver;
			const bool elfRead = parseSystemCnf(cnfText, boot, serial, ver) && ReadWholeFile(path / boot, elf);
			Judge(r, cnfText, elf, elfRead, expected);
			return r;
		}

		File f{OpenRead(path)};
		if (!f.f)
		{
			r.verdict = DiscVerdict::ReadError;
			r.message = "This file can't be opened. Check that it isn't in use, then choose it again.";
			return r;
		}
		uint8_t pvd[kSector];
		const bool isIso = ReadAt(f.f, 16ull * kSector, pvd, kSector) && pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0;
		if (!isIso)
		{
			// The boot ELF itself (outbreak.ini / command line): "\x7F" "ELF".
			uint8_t magic[4] = {};
			if (ReadAt(f.f, 0, magic, 4) && std::memcmp(magic, "\x7F" "ELF", 4) == 0)
			{
				r.stepsPassed = 1;
				std::vector<uint8_t> elf;
				const bool elfRead = ReadWholeFile(path, elf);
				const std::filesystem::path cnfPath = path.parent_path() / "SYSTEM.CNF";
				std::vector<uint8_t> cnf;
				std::string cnfText;
				if (ReadWholeFile(cnfPath, cnf))
					cnfText.assign(cnf.begin(), cnf.end());
				else
					cnfText = "BOOT2 = cdrom0:\\" + path.filename().string() + ";1"; // a bare ELF: its own name
				Judge(r, cnfText, elf, elfRead, expected);
				return r;
			}
			r.verdict = DiscVerdict::NotDiscImage;
			r.message = "This file can't be read as a disc image. It may be incomplete. Dump or copy it again.";
			return r;
		}
		r.stepsPassed = 1;
		const uint8_t* root = pvd + 156;
		const uint32_t rootLba = Le32(root + 2), rootSize = Le32(root + 10);
		uint32_t cnfLba = 0, cnfSize = 0;
		if (!FindInRoot(f.f, rootLba, rootSize, "SYSTEM.CNF", cnfLba, cnfSize) || cnfSize == 0 || cnfSize > 64u * 1024u)
		{
			r.verdict = DiscVerdict::NoSystemCnf;
			r.message = "This disc has no PS2 boot file (SYSTEM.CNF). Choose Resident Evil Outbreak File #1.";
			return r;
		}
		std::string cnfText(cnfSize, '\0');
		if (!ReadAt(f.f, uint64_t(cnfLba) * kSector, reinterpret_cast<uint8_t*>(cnfText.data()), cnfSize))
		{
			r.verdict = DiscVerdict::ReadError;
			r.message = "This disc image can't be read to the end. It may be incomplete. Dump or copy it again.";
			return r;
		}
		std::string boot, serial, ver;
		std::vector<uint8_t> elf;
		bool elfRead = false;
		uint32_t elfLba = 0, elfSize = 0;
		if (parseSystemCnf(cnfText, boot, serial, ver) && FindInRoot(f.f, rootLba, rootSize, boot, elfLba, elfSize) &&
			elfSize <= 64u * 1024u * 1024u)
		{
			elf.resize(elfSize);
			elfRead = ReadAt(f.f, uint64_t(elfLba) * kSector, elf.data(), elfSize);
		}
		Judge(r, cnfText, elf, elfRead, expected);
		return r;
	}

	namespace
	{
		bool Stat(const std::filesystem::path& p, uint64_t& size, int64_t& mtime)
		{
			std::error_code ec;
			const auto s = std::filesystem::is_directory(p, ec) ? 0 : std::filesystem::file_size(p, ec);
			if (ec)
				return false;
			const auto t = std::filesystem::last_write_time(p, ec);
			if (ec)
				return false;
			size = s;
			mtime = static_cast<int64_t>(t.time_since_epoch().count());
			return true;
		}
	} // namespace

	bool DiscCache::sameFileAs(const std::filesystem::path& p) const
	{
		uint64_t s = 0;
		int64_t t = 0;
		return valid && p == path && Stat(p, s, t) && s == size && t == mtime;
	}

	void DiscCache::remember(const std::filesystem::path& p, const DiscReport& r)
	{
		valid = Stat(p, size, mtime);
		path = p;
		report = r;
	}
} // namespace obui
