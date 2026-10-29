// memory_card: what the Memory cards tab shows and does with PCSX2 .ps2 cards (8 MB of 528-byte raw pages: 512 data +
// 16 ECC). Reading follows tools/ob/mcd.py info (mymcplus layout, which agrees with PCSX2 v2.9.134): the superblock, the
// root directory through the FAT. Mcd001.ps2 = slot 1, Mcd002.ps2 = slot 2 (PS2Recomp patch 0072, native/pcsx2_sio2).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obui
{
	struct CardInfo
	{
		bool present = false;
		bool formatted = false;
		uint64_t sizeBytes = 0;
		std::vector<std::string> saves; // root directory entries, without "." and ".." (File #1: BISLPM-65428NETBIO)
		std::string error;
	};
	CardInfo readCard(const std::filesystem::path& ps2File);

	// Copies the card to <folder>/backups/<stem>-YYYYMMDD-HHMMSS.ps2.bak and keeps the newest `keep` backups of it.
	bool backupCard(const std::filesystem::path& card, int keep, std::string* err);

	// The save folder changed: what happens to the cards in the old one (Mcd001.ps2 / Mcd002.ps2 only). A card that
	// already exists in the new folder is never overwritten: that card is left alone and named in err.
	enum class CardMove
	{
		Move,
		Copy,
		Leave,
	};
	bool transferCards(const std::filesystem::path& from, const std::filesystem::path& to, CardMove how, std::string* err);

	// Copies a .ps2 card (from PCSX2's memcards folder, say) in as <folder>/Mcd001.ps2, replacing it (the caller asks).
	bool importCard(const std::filesystem::path& src, const std::filesystem::path& folder, std::string* err);

	// Writing (tools/ob/mcd.py format / Card.add_dir): the network settings folder BWNETCNF the game needs before NET
	// CONNECTION, which the PS2 made with Sony's separate setup program (reports/2026-10-26-online-play.md entry 57).
	// A missing or all-0xFF card (8,650,752 bytes) becomes an empty formatted one, as mcd.py formats it (the game
	// accepts it). A formatted card is left alone (true); a card holding anything else is never formatted (false, err).
	bool formatCardIfBlank(const std::filesystem::path& ps2File, std::string* err);
	struct CardFile
	{
		std::string name;
		std::vector<uint8_t> data;
	};
	// Adds folder `name` with `files` to the root of a formatted card, laid out as MCMAN writes a save (mcd.py
	// Card.add_dir). False (err) when the card is unformatted or full, or a folder or file of that name is in the root:
	// nothing is overwritten. The card is written whole through a side file.
	bool addCardFolder(const std::filesystem::path& ps2File, const std::string& name, const std::vector<CardFile>& files,
		std::string* err);
	// A file in a root folder; false when the card, the folder or the file is not there.
	bool readCardFile(const std::filesystem::path& ps2File, const std::string& folder, const std::string& file,
		std::vector<uint8_t>& out);

	// At game start with online play on: the card in slot 1 holds BWNETCNF (`files`, the network settings folder), so
	// the game goes past NET CONNECTION without Sony's setup program. A missing or all-0xFF card is formatted first; an
	// existing BWNETCNF and every other save are left as they are.
	enum class NetworkFolder
	{
		AlreadyThere,
		Added,
		FormattedAndAdded,
		Failed, // err says why; the card is unchanged
	};
	NetworkFolder ensureNetworkFolder(const std::filesystem::path& ps2File, const std::vector<CardFile>& files, std::string* err);

	constexpr const char* kSlotCardNames[2] = {"Mcd001.ps2", "Mcd002.ps2"};
} // namespace obui
