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

	constexpr const char* kSlotCardNames[2] = {"Mcd001.ps2", "Mcd002.ps2"};
} // namespace obui
