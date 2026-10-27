// SPDX-License-Identifier: AGPL-3.0-or-later
#include "protocol.h"

#include <array>
#include <cstdio>

namespace ob::server
{
	std::vector<uint8_t> encode(const Packet& packet)
	{
		std::vector<uint8_t> out;
		out.reserve(kHeaderSize + packet.payload.size());
		out.push_back(packet.who);
		out.push_back(packet.kind);
		putU16(out, packet.cmd);
		putU16(out, static_cast<uint16_t>(packet.payload.size()));
		putU16(out, packet.pid);
		out.push_back(packet.error ? 0xFF : 0x00);
		out.insert(out.end(), {0xFF, 0xFF, 0xFF});
		out.insert(out.end(), packet.payload.begin(), packet.payload.end());
		return out;
	}

	namespace
	{
		Packet parseHeader(const uint8_t* p, size_t payloadSize)
		{
			Packet packet;
			packet.who = p[0];
			packet.kind = p[1];
			packet.cmd = static_cast<uint16_t>((p[2] << 8) | p[3]);
			packet.pid = static_cast<uint16_t>((p[6] << 8) | p[7]);
			packet.error = p[8] == 0xFF;
			packet.payload.assign(p + kHeaderSize, p + kHeaderSize + payloadSize);
			return packet;
		}

		void compactBuffer(std::vector<uint8_t>& buffer, size_t& start)
		{
			if (start == 0)
				return;
			buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(start));
			start = 0;
		}
	} // namespace

	void LobbyStream::feed(const uint8_t* data, size_t size)
	{
		compactBuffer(m_buffer, m_start);
		m_buffer.insert(m_buffer.end(), data, data + size);
	}

	std::optional<Packet> LobbyStream::next()
	{
		if (buffered() < kHeaderSize)
			return std::nullopt;
		const uint8_t* p = m_buffer.data() + m_start;
		const size_t length = (p[4] << 8) | p[5];
		if (buffered() < kHeaderSize + length)
			return std::nullopt;
		Packet packet = parseHeader(p, length);
		m_start += kHeaderSize + length;
		return packet;
	}

	void RelayStream::compact()
	{
		compactBuffer(m_buffer, m_start);
	}

	void RelayStream::feed(const uint8_t* data, size_t size)
	{
		compact();
		m_buffer.insert(m_buffer.end(), data, data + size);
	}

	std::optional<Packet> RelayStream::nextLogin()
	{
		if (m_loggedIn || m_broken || buffered() < kHeaderSize)
			return std::nullopt;
		const uint8_t* p = m_buffer.data() + m_start;
		const size_t length = (p[4] << 8) | p[5];
		if (buffered() < kHeaderSize + length)
			return std::nullopt;
		Packet packet = parseHeader(p, length);
		m_start += kHeaderSize + length;
		return packet;
	}

	std::optional<std::vector<uint8_t>> RelayStream::nextFrame()
	{
		if (!m_loggedIn || m_broken || buffered() < 1)
			return std::nullopt;
		const uint8_t* p = m_buffer.data() + m_start;
		const size_t length = p[0];
		if (length == 0)
		{
			m_broken = true;
			return std::nullopt;
		}
		if (buffered() < length)
			return std::nullopt;
		std::vector<uint8_t> frame(p, p + length);
		m_start += length;
		return frame;
	}

	uint8_t scrambleKey(size_t i, uint8_t pidLow)
	{
		static constexpr std::array<int, 8> kFix = {21, 23, 10, 17, 23, 19, 6, 13};
		static constexpr std::array<int, 8> kMask = {0x33, 0x30, 0x3C, 0x34, 0x2D, 0x30, 0x3C, 0x34};
		const int n = static_cast<int>(i & 0xFFFF), p = pidLow;
		const int value = kFix[n & 7] - (n & 0xF8) - p + (((p - 9 + n) & kMask[n & 7]) * 2);
		return static_cast<uint8_t>(value & 0xFF);
	}

	void scramble(uint8_t* data, size_t size, uint8_t pidLow)
	{
		for (size_t i = 0; i < size; i++)
			data[i] ^= scrambleKey(i, pidLow);
	}

	std::optional<std::string> readScrambledString(const std::vector<uint8_t>& payload, size_t offset, uint8_t pidLow,
		size_t* next)
	{
		if (offset + 4 > payload.size())
			return std::nullopt;
		const size_t length = getU16(payload, offset);
		if (length < 2 || offset + 2 + length > payload.size())
			return std::nullopt;
		std::string text(payload.begin() + static_cast<std::ptrdiff_t>(offset + 4),
			payload.begin() + static_cast<std::ptrdiff_t>(offset + 2 + length));
		scramble(reinterpret_cast<uint8_t*>(text.data()), text.size(), pidLow);
		if (next)
			*next = offset + 2 + length;
		return text;
	}

	void putString(std::vector<uint8_t>& out, const std::string& bytes)
	{
		putU16(out, static_cast<uint16_t>(bytes.size()));
		out.insert(out.end(), bytes.begin(), bytes.end());
	}

	void putU16(std::vector<uint8_t>& out, uint16_t value)
	{
		out.push_back(static_cast<uint8_t>(value >> 8));
		out.push_back(static_cast<uint8_t>(value));
	}

	void putU32(std::vector<uint8_t>& out, uint32_t value)
	{
		putU16(out, static_cast<uint16_t>(value >> 16));
		putU16(out, static_cast<uint16_t>(value));
	}

	uint16_t getU16(const std::vector<uint8_t>& payload, size_t offset)
	{
		if (offset + 2 > payload.size())
			return 0;
		return static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
	}

	std::string encodeSessionDigits(uint32_t session, uint16_t pid)
	{
		char out[16];
		std::snprintf(out, sizeof(out), "%05u%05u", session / 10000 + pid, session % 10000 + pid);
		return out;
	}

	std::optional<uint32_t> decodeSessionDigits(const uint8_t* digits, uint16_t pid)
	{
		uint32_t halves[2] = {};
		for (int h = 0; h < 2; h++)
			for (int i = 0; i < 5; i++)
			{
				const uint8_t c = digits[h * 5 + i];
				if (c < '0' || c > '9')
					return std::nullopt;
				halves[h] = halves[h] * 10 + (c - '0');
			}
		if (halves[0] < pid || halves[1] < pid || halves[0] - pid > 9999 || halves[1] - pid > 9999)
			return std::nullopt;
		const uint32_t session = (halves[0] - pid) * 10000 + (halves[1] - pid);
		if (session < 10000000u)
			return std::nullopt;
		return session;
	}
}
