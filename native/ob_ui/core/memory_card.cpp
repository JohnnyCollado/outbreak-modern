// memory_card: see memory_card.h. Offsets: superblock (page 0) pages_per_cluster at 0x2A, alloc_offset at 0x34,
// rootdir_cluster at 0x3C, first indirect FAT cluster at 0x50; a directory entry is one 512-byte page: mode at 0,
// length at 4, first cluster at 0x10, name at 0x40 (tools/ob/mcd.py _entries / info).
#include "memory_card.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <system_error>

namespace obui
{
	namespace
	{
		constexpr uint32_t kPage = 512, kRaw = 528;
		constexpr char kMagic[] = "Sony PS2 Memory Card Format ";

		uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
		uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

		struct Card
		{
			const std::vector<uint8_t>& d;
			uint32_t ppc, alloc, ifc;

			const uint8_t* Page(uint64_t page) const
			{
				const uint64_t off = page * kRaw;
				return off + kPage <= d.size() ? d.data() + off : nullptr;
			}
			// The FAT entry of a cluster, through the first indirect FAT cluster (mcd.py fat()).
			bool Fat(uint32_t c, uint32_t& out) const
			{
				const uint32_t epc = kPage * ppc / 4; // entries per cluster
				const uint32_t perPage = kPage / 4;
				const uint32_t indirectIndex = c / epc / epc;
				const uint8_t* ip = Page(uint64_t(ifc) * ppc + indirectIndex / perPage);
				if (!ip)
					return false;
				const uint32_t fatCluster = Le32(ip + ((c / epc) % perPage) * 4);
				const uint8_t* fp = Page(uint64_t(fatCluster) * ppc + (c % epc) / perPage);
				if (!fp)
					return false;
				out = Le32(fp + (c % perPage) * 4);
				return true;
			}
			// A directory's entry names, following its FAT chain (one entry per page).
			bool Entries(uint32_t cluster, uint32_t count, std::vector<std::string>& names) const
			{
				for (int guard = 0; names.size() < count && guard < 65536; ++guard)
				{
					for (uint32_t p = 0; p < ppc && names.size() < count; ++p)
					{
						const uint8_t* e = Page((uint64_t(alloc) + cluster) * ppc + p);
						if (!e)
							return false;
						const char* name = reinterpret_cast<const char*>(e + 0x40);
						names.emplace_back(name, strnlen(name, 32));
					}
					if (names.size() >= count)
						break;
					uint32_t next = 0;
					if (!Fat(cluster, next) || next == 0xFFFFFFFFu || !(next & 0x80000000u))
						break;
					cluster = next & 0x7FFFFFFFu;
				}
				return true;
			}
		};

		bool ReadAll(const std::filesystem::path& p, std::vector<uint8_t>& out)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f)
				return false;
			f.seekg(0, std::ios::end);
			const std::streamoff n = f.tellg();
			if (n < 0)
				return false;
			out.resize(static_cast<size_t>(n));
			f.seekg(0);
			f.read(reinterpret_cast<char*>(out.data()), n);
			return static_cast<bool>(f);
		}

		bool CopyCardFile(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite, std::string* err)
		{
			std::error_code ec;
			std::filesystem::copy_file(from, to,
				overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none, ec);
			if (ec && err)
				*err = "Couldn't copy " + from.filename().string() + " to " + to.parent_path().string() + ": " + ec.message();
			return !ec;
		}
	} // namespace

	CardInfo readCard(const std::filesystem::path& ps2File)
	{
		CardInfo info;
		std::error_code ec;
		if (!std::filesystem::is_regular_file(ps2File, ec))
			return info;
		info.present = true;
		std::vector<uint8_t> d;
		if (!ReadAll(ps2File, d))
		{
			info.error = "This card can't be read.";
			return info;
		}
		info.sizeBytes = d.size();
		if (d.size() < kRaw || std::memcmp(d.data(), kMagic, sizeof(kMagic) - 1) != 0)
			return info; // unformatted (a new card is all 0xFF): the game offers to format it at the first save
		info.formatted = true;
		const uint8_t* sb = d.data();
		Card card{d, Le16(sb + 0x2A), Le32(sb + 0x34), Le32(sb + 0x50)};
		const uint32_t root = Le32(sb + 0x3C);
		if (card.ppc == 0 || card.ppc > 16)
		{
			info.error = "This card's format isn't recognised.";
			return info;
		}
		const uint8_t* dot = card.Page((uint64_t(card.alloc) + root) * card.ppc);
		if (!dot)
		{
			info.error = "This card is shorter than its format says.";
			return info;
		}
		const uint32_t count = Le32(dot + 4);
		std::vector<std::string> names;
		if (count > 4096 || !card.Entries(root, count, names))
		{
			info.error = "This card's directory can't be read.";
			return info;
		}
		for (const std::string& n : names)
			if (n != "." && n != ".." && !n.empty())
				info.saves.push_back(n);
		return info;
	}

	bool backupCard(const std::filesystem::path& card, int keep, std::string* err)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(card, ec))
			return true; // nothing to back up yet
		const std::filesystem::path dir = card.parent_path() / "backups";
		std::filesystem::create_directories(dir, ec);
		if (ec)
		{
			if (err)
				*err = "Couldn't make the backups folder: " + ec.message();
			return false;
		}
		const std::time_t t = std::time(nullptr);
		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		char stamp[32];
		std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
		const std::string stem = card.stem().string();
		if (!CopyCardFile(card, dir / (stem + "-" + stamp + ".ps2.bak"), true, err))
			return false;
		// Keep the newest `keep`: names sort by time (fixed-width stamp).
		std::vector<std::filesystem::path> mine;
		for (const auto& e : std::filesystem::directory_iterator(dir, ec))
		{
			const std::string n = e.path().filename().string();
			if (n.rfind(stem + "-", 0) == 0 && n.size() > 8 && n.compare(n.size() - 8, 8, ".ps2.bak") == 0)
				mine.push_back(e.path());
		}
		std::sort(mine.begin(), mine.end());
		for (size_t i = 0; keep >= 0 && i + static_cast<size_t>(keep) < mine.size(); ++i)
			std::filesystem::remove(mine[i], ec);
		return true;
	}

	bool transferCards(const std::filesystem::path& from, const std::filesystem::path& to, CardMove how, std::string* err)
	{
		if (how == CardMove::Leave)
			return true;
		std::error_code ec;
		if (std::filesystem::equivalent(from, to, ec))
			return true;
		std::filesystem::create_directories(to, ec);
		if (ec)
		{
			if (err)
				*err = "Couldn't make the folder " + to.string() + ": " + ec.message();
			return false;
		}
		bool ok = true;
		std::string kept;
		for (const char* name : kSlotCardNames)
		{
			const std::filesystem::path src = from / name, dst = to / name;
			if (!std::filesystem::is_regular_file(src, ec))
				continue;
			if (std::filesystem::exists(dst, ec))
			{
				kept += (kept.empty() ? "" : ", ") + std::string(name);
				ok = false;
				continue;
			}
			std::string e;
			if (!CopyCardFile(src, dst, false, &e))
			{
				if (err)
					*err = e;
				return false;
			}
			if (how == CardMove::Move)
				std::filesystem::remove(src, ec);
		}
		if (!kept.empty() && err)
			*err = "The new folder already has " + kept + ". It was left as it is, and the old card was not copied over it.";
		return ok;
	}

	bool importCard(const std::filesystem::path& src, const std::filesystem::path& folder, std::string* err)
	{
		std::error_code ec;
		std::filesystem::create_directories(folder, ec);
		const CardInfo info = readCard(src);
		if (!info.present || info.sizeBytes == 0 || info.sizeBytes % kRaw != 0)
		{
			if (err)
				*err = "This file isn't a PCSX2 memory card (.ps2).";
			return false;
		}
		return CopyCardFile(src, folder / kSlotCardNames[0], true, err);
	}
} // namespace obui
