// SPDX-License-Identifier: AGPL-3.0-or-later
#include "datafile.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace ob::server
{
	namespace
	{
		struct Scenario
		{
			const char* title;       // Shift-JIS, as the main program's table D_002167B0 has it
			const char* description; // the game's text markup (as the lobby's error messages use it)
		};
		// File #1's five scenarios in the game's order (main program 0x231C48-0x231C68).
		const Scenario kScenarios[kScenarioCount] = {
			{"\x94\xAD\x90\xB6", "<BODY>Outbreak<END>"},              // 発生
			{"\x97\xEB\x89\xBA", "<BODY>Below Freezing Point<END>"},  // 零下
			{"\x91\x83\x8C" "\x41", "<BODY>The Hive<END>"},           // 巣窟
			{"\x8D\x96\x89\x8A", "<BODY>Hellfire<END>"},              // 獄炎
			{"\x8C\x88\x88\xD3", "<BODY>Decisions, Decisions<END>"}, // 決意
		};

		void putLe32(std::vector<uint8_t>& f, size_t at, int32_t v)
		{
			const uint32_t u = static_cast<uint32_t>(v);
			f[at] = static_cast<uint8_t>(u);
			f[at + 1] = static_cast<uint8_t>(u >> 8);
			f[at + 2] = static_cast<uint8_t>(u >> 16);
			f[at + 3] = static_cast<uint8_t>(u >> 24);
		}

		void putBe32(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v >> 24));
			out.push_back(static_cast<uint8_t>(v >> 16));
			out.push_back(static_cast<uint8_t>(v >> 8));
			out.push_back(static_cast<uint8_t>(v));
		}

		uint32_t getBe32(const std::vector<uint8_t>& p, size_t at)
		{
			return at + 4 <= p.size()
				? static_cast<uint32_t>(p[at]) << 24 | p[at + 1] << 16 | p[at + 2] << 8 | p[at + 3]
				: 0;
		}
	}

	std::vector<uint8_t> loginDataFile()
	{
		std::vector<uint8_t> f(kDataText, 0);
		std::string text; // the text area, appended after kDataText
		for (size_t i = 0; i < kDataRecords; i++)
			putLe32(f, i * kDataRecordSize + kDataRecordText, -1); // absent
		for (int i = 0; i < kScenarioCount; i++)
		{
			uint8_t* r = &f[i * kDataRecordSize];
			r[0] = 1; // not read by NETWK's helpers; set as a present record's (unverified)
			r[1] = 1; // NETWK adds 0x5F to it (a label or picture index; unverified)
			r[2] = static_cast<uint8_t>(i + 1); // the scenario number (unverified: kept by func_005D1670)
			std::memcpy(r + kDataRecordName, kScenarios[i].title, std::strlen(kScenarios[i].title));
			putLe32(f, i * kDataRecordSize + kDataRecordText, static_cast<int32_t>(text.size()));
			text += kScenarios[i].description;
			text += '\0';
		}
		f[kDataEnabled] = 1;
		const char* heading = "BIOHAZARD OUTBREAK";
		std::memcpy(&f[kDataTitles], heading, std::strlen(heading));
		f.insert(f.end(), text.begin(), text.end());
		return f;
	}

	std::vector<uint8_t> infoSizesAnswer(const std::vector<uint8_t>& piece0)
	{
		std::vector<uint8_t> out = {1};
		putBe32(out, static_cast<uint32_t>(piece0.size()));
		return out;
	}

	std::vector<uint8_t> infoChunkAnswer(const std::vector<uint8_t>& piece0, const std::vector<uint8_t>& request)
	{
		const uint8_t piece = request.empty() ? 0 : request[0];
		const uint32_t offset = getBe32(request, 1);
		const uint32_t asked = request.size() >= 9 ? getBe32(request, 5) : static_cast<uint32_t>(piece0.size());
		const uint32_t left = piece == 0 && offset < piece0.size() ? static_cast<uint32_t>(piece0.size() - offset) : 0;
		const uint32_t size = std::min(asked, left);
		std::vector<uint8_t> out = {piece};
		putBe32(out, offset);
		putBe32(out, size);
		out.push_back(static_cast<uint8_t>(size >> 8));
		out.push_back(static_cast<uint8_t>(size));
		out.insert(out.end(), piece0.begin() + offset * (size != 0), piece0.begin() + offset * (size != 0) + size);
		return out;
	}
}
