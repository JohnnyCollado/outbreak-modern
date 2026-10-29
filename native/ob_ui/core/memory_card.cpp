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

	// ---- writing: tools/ob/mcd.py format_card, raw_page/ecc128, Card (FAT chains, add_dir) ----
	namespace
	{
		constexpr uint32_t kPages = 16384, kPpc = 2, kPpb = 16, kClusters = kPages / kPpc, kBlocks = kPages / kPpb;
		constexpr uint32_t kFirstIfc = 8, kFatClusters = 32, kAllocOffset = kFirstIfc + 1 + kFatClusters; // 41
		constexpr uint32_t kGood1 = kBlocks - 1, kGood2 = kBlocks - 2;
		constexpr uint32_t kAllocEnd = kGood2 * (kPpb / kPpc) - kAllocOffset; // 8135
		constexpr uint32_t kFatChain = 0x80000000u, kFatEnd = 0xFFFFFFFFu, kFatFree = 0x7FFFFFFFu;
		constexpr uint16_t kModeDir = 0x8427, kModeFile = 0x8497, kModeDotDot = 0xA426;
		constexpr uint64_t kCardBytes = uint64_t(kPages) * kRaw;

		void PutLe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
		void PutLe32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }

		// PCSX2 CalculateECC (mcd.py ecc128): three bytes per 128-byte chunk.
		void Ecc128(const uint8_t* chunk, uint8_t out[3])
		{
			static uint8_t parity[256], columnMask[256];
			static const bool ready = [] {
				for (int b = 0; b < 256; b++)
				{
					int n = 0;
					for (int i = 0; i < 8; i++)
						n += (b >> i) & 1;
					parity[b] = uint8_t(n & 1);
				}
				for (int b = 0; b < 256; b++)
					columnMask[b] = uint8_t((parity[b & 0x55] ? 0x01 : 0) | (parity[b & 0x33] ? 0x02 : 0) | (parity[b & 0x0F] ? 0x04 : 0) |
											(parity[b & 0xAA] ? 0x10 : 0) | (parity[b & 0xCC] ? 0x20 : 0) | (parity[b & 0xF0] ? 0x40 : 0));
				return true;
			}();
			(void)ready;
			uint8_t column = 0x77, line0 = 0x7F, line1 = 0x7F;
			for (int i = 0; i < 128; i++)
			{
				column ^= columnMask[chunk[i]];
				if (parity[chunk[i]])
				{
					line0 ^= uint8_t(~i);
					line1 ^= uint8_t(i);
				}
			}
			out[0] = column;
			out[1] = line0 & 0x7F;
			out[2] = line1 & 0x7F;
		}

		// A PS2 timestamp (mcd.py tod: pad, second, minute, hour, day, month, year).
		void Tod(uint8_t* p, const std::tm& t)
		{
			p[0] = 0;
			p[1] = uint8_t(t.tm_sec);
			p[2] = uint8_t(t.tm_min);
			p[3] = uint8_t(t.tm_hour);
			p[4] = uint8_t(t.tm_mday);
			p[5] = uint8_t(t.tm_mon + 1);
			PutLe16(p + 6, uint16_t(t.tm_year + 1900));
		}

		std::tm Now()
		{
			const std::time_t now = std::time(nullptr);
			std::tm t{};
#ifdef _WIN32
			localtime_s(&t, &now);
#else
			localtime_r(&now, &t);
#endif
			return t;
		}

		// A directory entry page (mcd.py "<HHL8sLL8sL28x448s": mode, attr, length, created, cluster, dir_entry,
		// modified, attr, name at 0x40).
		std::vector<uint8_t> Dirent(uint16_t mode, uint32_t length, uint32_t cluster, uint32_t dirEntry, const std::tm& t,
			const std::string& name)
		{
			std::vector<uint8_t> e(kPage, 0);
			PutLe16(e.data(), mode);
			PutLe32(e.data() + 4, length);
			Tod(e.data() + 8, t);
			PutLe32(e.data() + 0x10, cluster);
			PutLe32(e.data() + 0x14, dirEntry);
			Tod(e.data() + 0x18, t);
			std::memcpy(e.data() + 0x40, name.data(), std::min<size_t>(name.size(), 32));
			return e;
		}

		struct Writer
		{
			std::vector<uint8_t> d;
			uint32_t ppc = kPpc, alloc = kAllocOffset, allocEnd = kAllocEnd, root = 0, ifc[32] = {};

			uint32_t ClusterBytes() const { return kPage * ppc; }
			uint32_t Epc() const { return ClusterBytes() / 4; }
			uint8_t* PageData(uint64_t page) { return d.data() + page * kRaw; }

			// mcd.py raw_page: data padded to 512, ECC of each 128 bytes, four 0xFF.
			void PutPage(uint64_t page, const uint8_t* data, size_t size)
			{
				uint8_t* p = PageData(page);
				std::memset(p, 0, kPage);
				std::memcpy(p, data, std::min<size_t>(size, kPage));
				for (int i = 0; i < 4; i++)
					Ecc128(p + i * 128, p + kPage + i * 3);
				std::memset(p + kPage + 12, 0xFF, 4);
			}
			void PutCluster(uint32_t absolute, const uint8_t* data, size_t size)
			{
				for (uint32_t i = 0; i < ppc; i++)
				{
					const size_t off = size_t(i) * kPage;
					PutPage(uint64_t(absolute) * ppc + i, off < size ? data + off : nullptr, off < size ? size - off : 0);
				}
			}
			// The FAT word of data cluster c: its absolute page and offset (mcd.py _fat_slot).
			uint8_t* FatWord(uint32_t c)
			{
				const uint32_t epc = Epc(), perPage = kPage / 4;
				const uint32_t indirect = ifc[c / epc / epc];
				const uint32_t fatCluster = Le32(PageData(uint64_t(indirect) * ppc + (c / epc % epc) / perPage) + ((c / epc % epc) % perPage) * 4);
				return PageData(uint64_t(fatCluster) * ppc + (c % epc) / perPage) + (c % perPage) * 4;
			}
			uint32_t Fat(uint32_t c) { return Le32(FatWord(c)); }
			void SetFat(uint32_t c, uint32_t v)
			{
				uint8_t* w = FatWord(c);
				PutLe32(w, v);
				const uint64_t page = uint64_t(w - d.data()) / kRaw;
				std::vector<uint8_t> copy(PageData(page), PageData(page) + kPage);
				PutPage(page, copy.data(), kPage); // the page's ECC follows its new contents
			}
			bool Chain(uint32_t c, std::vector<uint32_t>& out)
			{
				out.assign(1, c);
				for (uint32_t guard = 0; guard < kClusters; guard++)
				{
					const uint32_t next = Fat(out.back());
					if (next == kFatEnd)
						return true;
					if (!(next & kFatChain))
						return false;
					out.push_back(next & ~kFatChain);
				}
				return false;
			}
			// count free clusters, first fit, chained; linked after `after` when given (mcd.py allocate).
			bool Allocate(uint32_t count, std::vector<uint32_t>& out, int64_t after = -1)
			{
				out.clear();
				for (uint32_t c = 0; c < allocEnd && out.size() < count; c++)
					if (Fat(c) == kFatFree)
						out.push_back(c);
				if (out.size() < count)
					return false;
				for (size_t i = 0; i + 1 < out.size(); i++)
					SetFat(out[i], kFatChain | out[i + 1]);
				SetFat(out.back(), kFatEnd);
				if (after >= 0)
					SetFat(uint32_t(after), kFatChain | out.front());
				return true;
			}
			// The absolute page of entry `index` of the folder at cluster `first`, growing its chain (mcd.py entry_page).
			bool EntryPage(uint32_t first, uint32_t index, uint64_t& page)
			{
				std::vector<uint32_t> chain;
				if (!Chain(first, chain))
					return false;
				while (index / ppc >= chain.size())
				{
					std::vector<uint32_t> added;
					if (!Allocate(1, added, chain.back()))
						return false;
					PutCluster(alloc + added[0], nullptr, 0);
					chain.push_back(added[0]);
				}
				page = uint64_t(alloc + chain[index / ppc]) * ppc + index % ppc;
				return true;
			}
			// Entry `name` in the folder at `first` with `count` entries: its index (or -1).
			int64_t Find(uint32_t first, uint32_t count, const std::string& name)
			{
				for (uint32_t i = 0; i < count; i++)
				{
					uint64_t page = 0;
					if (!EntryPage(first, i, page))
						return -1;
					const uint8_t* e = PageData(page);
					if ((Le16(e) & 0x8000) && strnlen(reinterpret_cast<const char*>(e + 0x40), 32) == name.size() &&
						std::memcmp(e + 0x40, name.data(), name.size()) == 0)
						return i;
				}
				return -1;
			}
		};

		bool LoadFormatted(const std::filesystem::path& p, Writer& w, std::string* err)
		{
			if (!ReadAll(p, w.d) || w.d.size() != kCardBytes || std::memcmp(w.d.data(), kMagic, sizeof(kMagic) - 1) != 0)
			{
				if (err)
					*err = "This memory card isn't formatted.";
				return false;
			}
			const uint8_t* sb = w.d.data();
			w.ppc = Le16(sb + 0x2A);
			w.alloc = Le32(sb + 0x34);
			w.allocEnd = Le32(sb + 0x38);
			w.root = Le32(sb + 0x3C);
			for (int i = 0; i < 32; i++)
				w.ifc[i] = Le32(sb + 0x50 + 4 * i);
			if (w.ppc != kPpc || w.allocEnd > kClusters)
			{
				if (err)
					*err = "This card's format isn't recognised.";
				return false;
			}
			return true;
		}

		// The whole card through a side file, so a failed write leaves the old card.
		bool SaveWhole(const std::filesystem::path& p, const std::vector<uint8_t>& d, std::string* err)
		{
			const std::filesystem::path tmp = p.string() + ".writing";
			{
				std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
				f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
				if (!f)
				{
					if (err)
						*err = "Couldn't write the memory card.";
					return false;
				}
			}
			std::error_code ec;
			std::filesystem::rename(tmp, p, ec);
			if (ec)
			{
				std::filesystem::remove(tmp, ec);
				if (err)
					*err = "Couldn't replace the memory card: " + ec.message();
				return false;
			}
			return true;
		}
	} // namespace

	bool formatCardIfBlank(const std::filesystem::path& ps2File, std::string* err)
	{
		std::error_code ec;
		if (std::filesystem::exists(ps2File, ec))
		{
			std::vector<uint8_t> d;
			if (!ReadAll(ps2File, d))
			{
				if (err)
					*err = "This card can't be read.";
				return false;
			}
			if (d.size() >= kRaw && std::memcmp(d.data(), kMagic, sizeof(kMagic) - 1) == 0)
				return true;
			if (d.size() != kCardBytes || std::any_of(d.begin(), d.end(), [](uint8_t b) { return b != 0xFF; }))
			{
				if (err)
					*err = "This memory card holds data that isn't a formatted card; it was left alone.";
				return false;
			}
		}
		Writer w;
		w.d.assign(kCardBytes, 0);
		for (uint32_t p = 0; p < kPages; p++)
			w.PutPage(p, nullptr, 0);
		// Superblock (mcd.py "<28s12sHHHHLLLLLL8x128s128sbbxx").
		std::vector<uint8_t> sb(kPage, 0);
		std::memcpy(sb.data(), kMagic, sizeof(kMagic) - 1);
		std::memcpy(sb.data() + 0x1C, "1.2.0.0", 7);
		PutLe16(sb.data() + 0x28, kPage);
		PutLe16(sb.data() + 0x2A, kPpc);
		PutLe16(sb.data() + 0x2C, kPpb);
		PutLe16(sb.data() + 0x2E, 0xFF00);
		PutLe32(sb.data() + 0x30, kClusters);
		PutLe32(sb.data() + 0x34, kAllocOffset);
		PutLe32(sb.data() + 0x38, kAllocEnd);
		PutLe32(sb.data() + 0x3C, 0);
		PutLe32(sb.data() + 0x40, kGood1);
		PutLe32(sb.data() + 0x44, kGood2);
		PutLe32(sb.data() + 0x50, kFirstIfc);
		std::memset(sb.data() + 0xD0, 0xFF, 128);
		sb[0x150] = 2;
		sb[0x151] = 0x2B;
		w.PutPage(0, sb.data(), sb.size());
		const uint32_t epc = w.Epc();
		std::vector<uint8_t> cluster(w.ClusterBytes(), 0);
		for (uint32_t i = 0; i < epc; i++)
			PutLe32(cluster.data() + 4 * i, i < kFatClusters ? kFirstIfc + 1 + i : 0xFFFFFFFFu);
		w.PutCluster(kFirstIfc, cluster.data(), cluster.size());
		for (uint32_t f = 0; f < kFatClusters; f++)
		{
			for (uint32_t i = 0; i < epc; i++)
			{
				const uint32_t c = f * epc + i;
				PutLe32(cluster.data() + 4 * i, c == 0 ? kFatEnd : c < kAllocEnd ? kFatFree : kFatEnd);
			}
			w.PutCluster(kFirstIfc + 1 + f, cluster.data(), cluster.size());
		}
		const std::tm t = Now();
		std::vector<uint8_t> rootDir = Dirent(kModeDir, 2, 0, 0, t, ".");
		const std::vector<uint8_t> dotdot = Dirent(kModeDotDot, 0, 0, 0, t, "..");
		rootDir.insert(rootDir.end(), dotdot.begin(), dotdot.end());
		w.PutCluster(kAllocOffset, rootDir.data(), rootDir.size());
		for (uint32_t p = kGood2 * kPpb; p < (kGood2 + 1) * kPpb; p++)
			std::memset(w.PageData(p), 0xFF, kRaw);
		std::filesystem::create_directories(ps2File.parent_path(), ec);
		return SaveWhole(ps2File, w.d, err);
	}

	bool addCardFolder(const std::filesystem::path& ps2File, const std::string& name, const std::vector<CardFile>& files,
		std::string* err)
	{
		Writer w;
		if (!LoadFormatted(ps2File, w, err))
			return false;
		auto fail = [&](const std::string& why) {
			if (err)
				*err = why;
			return false;
		};
		uint64_t rootDotPage = 0;
		if (!w.EntryPage(w.root, 0, rootDotPage))
			return fail("This card's directory can't be read.");
		const uint32_t rootLen = Le32(w.PageData(rootDotPage) + 4);
		if (w.Find(w.root, rootLen, name) >= 0)
			return fail(name + " is already on this card; it was left as it is.");
		const std::tm t = Now();
		const uint32_t count = 2 + uint32_t(files.size());
		std::vector<uint32_t> dirClusters;
		if (!w.Allocate((count + w.ppc - 1) / w.ppc, dirClusters))
			return fail("This memory card is full.");
		for (uint32_t c : dirClusters)
			w.PutCluster(w.alloc + c, nullptr, 0);
		const uint32_t first = dirClusters[0];
		std::vector<std::vector<uint8_t>> entries;
		entries.push_back(Dirent(kModeDir, 0, w.root, rootLen, t, "."));
		entries.push_back(Dirent(kModeDir, 0, 0, 0, t, ".."));
		const uint32_t size = w.ClusterBytes();
		for (const CardFile& f : files)
		{
			const uint32_t n = std::max<uint32_t>(1, uint32_t((f.data.size() + size - 1) / size));
			std::vector<uint32_t> chain;
			if (!w.Allocate(n, chain))
				return fail("This memory card is full.");
			for (uint32_t i = 0; i < n; i++)
			{
				const size_t off = size_t(i) * size;
				w.PutCluster(w.alloc + chain[i], off < f.data.size() ? f.data.data() + off : nullptr,
					off < f.data.size() ? f.data.size() - off : 0);
			}
			entries.push_back(Dirent(kModeFile, uint32_t(f.data.size()), chain[0], 0, t, f.name));
		}
		for (uint32_t i = 0; i < entries.size(); i++)
		{
			uint64_t page = 0;
			if (!w.EntryPage(first, i, page))
				return fail("This memory card is full.");
			w.PutPage(page, entries[i].data(), entries[i].size());
		}
		uint64_t slot = 0;
		if (!w.EntryPage(w.root, rootLen, slot))
			return fail("This memory card is full.");
		const std::vector<uint8_t> folder = Dirent(kModeDir, count, first, 0, t, name);
		w.PutPage(slot, folder.data(), folder.size());
		if (!w.EntryPage(w.root, 0, rootDotPage)) // the chain may have grown
			return fail("This card's directory can't be read.");
		std::vector<uint8_t> dot(w.PageData(rootDotPage), w.PageData(rootDotPage) + kPage);
		PutLe32(dot.data() + 4, rootLen + 1);
		w.PutPage(rootDotPage, dot.data(), dot.size());
		return SaveWhole(ps2File, w.d, err);
	}

	NetworkFolder ensureNetworkFolder(const std::filesystem::path& ps2File, const std::vector<CardFile>& files, std::string* err)
	{
		const CardInfo before = readCard(ps2File);
		if (before.formatted && std::find(before.saves.begin(), before.saves.end(), "BWNETCNF") != before.saves.end())
			return NetworkFolder::AlreadyThere;
		if (!formatCardIfBlank(ps2File, err))
			return NetworkFolder::Failed;
		if (!addCardFolder(ps2File, "BWNETCNF", files, err))
			return NetworkFolder::Failed;
		return before.formatted ? NetworkFolder::Added : NetworkFolder::FormattedAndAdded;
	}

	bool readCardFile(const std::filesystem::path& ps2File, const std::string& folder, const std::string& file,
		std::vector<uint8_t>& out)
	{
		Writer w;
		if (!LoadFormatted(ps2File, w, nullptr))
			return false;
		uint64_t page = 0;
		if (!w.EntryPage(w.root, 0, page))
			return false;
		const int64_t fi = w.Find(w.root, Le32(w.PageData(page) + 4), folder);
		if (fi < 0 || !w.EntryPage(w.root, uint32_t(fi), page))
			return false;
		const uint8_t* fe = w.PageData(page);
		const uint32_t dirFirst = Le32(fe + 0x10), dirLen = Le32(fe + 4);
		const int64_t ei = w.Find(dirFirst, dirLen, file);
		if (ei < 0 || !w.EntryPage(dirFirst, uint32_t(ei), page))
			return false;
		const uint8_t* e = w.PageData(page);
		const uint32_t length = Le32(e + 4);
		std::vector<uint32_t> chain;
		if (!w.Chain(Le32(e + 0x10), chain))
			return false;
		out.clear();
		for (uint32_t c : chain)
			for (uint32_t p = 0; p < w.ppc && out.size() < length; p++)
			{
				const uint8_t* src = w.PageData(uint64_t(w.alloc + c) * w.ppc + p);
				out.insert(out.end(), src, src + std::min<size_t>(kPage, length - out.size()));
			}
		return out.size() == length;
	}
} // namespace obui
