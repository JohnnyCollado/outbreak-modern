// SPDX-License-Identifier: AGPL-3.0-or-later
// datafile: the data file the lobby hands the game at login (6881 = piece sizes, 6882 = bytes). Its first piece is
// the room rules' scenario list (reports/2026-10-26-online-play.md entries 42-43, read from NETWK's code): 72-byte
// records, an on/off byte, title strings and a text area. Built here from that layout; no bytes from other servers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ob::server
{
	// Piece 0 layout (offsets from its start; the game copies it to its state block + 0x15540).
	constexpr size_t kDataMax = 0x2100;      // the game clears and copies at most this many bytes
	constexpr size_t kDataRecordSize = 0x48; // one scenario record
	constexpr size_t kDataRecords = 48;      // records up to 0xD80
	constexpr size_t kDataRecordName = 3;    // the name (Shift-JIS) from byte 3
	constexpr size_t kDataRecordText = 0x44; // s32, little-endian (an EE lw): offset into the text area; < 0 = none
	constexpr size_t kDataEnabled = 0xD83;   // nonzero = the scenario screens are on
	constexpr size_t kDataTitles = 0xDA8;    // 32-byte strings
	constexpr size_t kDataText = 0xE08;      // the text area record +0x44 points into

	// The File #1 scenarios in the game's order, Shift-JIS (the main program's table D_002167B0).
	constexpr int kScenarioCount = 5;

	std::vector<uint8_t> loginDataFile();
	// 6881: one count byte, then each piece's size as 4 big-endian bytes (NETWK func_005587A0 / func_005587D0).
	std::vector<uint8_t> infoSizesAnswer(const std::vector<uint8_t>& piece0);
	// 6882: the request is [piece:1][offset:4 BE][size:4 BE]; the answer [piece:1][offset:4 BE][size:4 BE][size low
	// 2 bytes][bytes], the size clamped to what is left.
	std::vector<uint8_t> infoChunkAnswer(const std::vector<uint8_t>& piece0, const std::vector<uint8_t>& request);
}
