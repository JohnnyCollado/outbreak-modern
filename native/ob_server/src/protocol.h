// SPDX-License-Identifier: AGPL-3.0-or-later
// protocol: the wire format Biohazard Outbreak File #1 uses with its lobby (TCP 8300) and relay (TCP 8690), as written
// down in research/2026-10-27-lobby-protocol.md section 3: the 12-byte packet header, the stream splitters for both
// ports, the scrambling of strings the game sends, and the session number's 10-digit form.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ob::server
{
	// Header byte 0: who sent it.
	enum class Who : uint8_t
	{
		Server = 0x18,      // lobby
		Client = 0x81,      // the game, to the lobby
		RelayServer = 0x28, // relay
		RelayClient = 0x82, // the game, to the relay
	};

	// Header byte 1: what kind of packet.
	enum class Kind : uint8_t
	{
		Query = 0x01,     // asks; the other side answers with the same packet id
		Tell = 0x02,      // the answer
		Broadcast = 0x10, // no answer expected
	};

	constexpr size_t kHeaderSize = 12;

	struct Packet
	{
		uint8_t who = 0;
		uint8_t kind = 0;
		uint16_t cmd = 0;
		uint16_t pid = 0;  // packet id: an answer repeats the question's
		bool error = false; // header byte 8 = 0xFF: the payload is a message for the player
		std::vector<uint8_t> payload;
	};

	// The packet as sent: header (length = payload size, bytes 9..11 = FF FF FF) + payload. Payloads over 65,535 bytes
	// are a programming error (none comes close: the largest is ~5 KB).
	std::vector<uint8_t> encode(const Packet& packet);

	// Splits the lobby's TCP stream into packets: each is 12 + length bytes. Partial packets wait for more bytes.
	class LobbyStream
	{
	public:
		void feed(const uint8_t* data, size_t size);
		std::optional<Packet> next();       // the next complete packet, if one is buffered
		size_t buffered() const { return m_buffer.size() - m_start; }
		// More than this many bytes waiting without a complete packet: the peer is not speaking this protocol.
		static constexpr size_t kMaxBuffered = 1u << 17;
		bool overflowed() const { return buffered() > kMaxBuffered; }

	private:
		std::vector<uint8_t> m_buffer;
		size_t m_start = 0;
	};

	// Splits the relay's TCP stream. Until the game has logged in (loggedIn = false) it sends one packet with the
	// lobby's 12-byte header (GSLOGIN, who = RelayClient); after that, game frames: byte 0 = the frame's whole length,
	// itself included (1..255), then data the server never reads. A length of 0 is a broken stream (the fan server
	// loops forever on it): broken() turns true and nothing more comes out.
	class RelayStream
	{
	public:
		void feed(const uint8_t* data, size_t size);
		std::optional<Packet> nextLogin();              // while not logged in
		std::optional<std::vector<uint8_t>> nextFrame(); // after: the frame as received, length byte included
		void setLoggedIn() { m_loggedIn = true; }
		bool loggedIn() const { return m_loggedIn; }
		bool broken() const { return m_broken; }
		size_t buffered() const { return m_buffer.size() - m_start; }

	private:
		void compact();
		std::vector<uint8_t> m_buffer;
		size_t m_start = 0;
		bool m_loggedIn = false;
		bool m_broken = false;
	};

	// The key byte the game XORs into byte i of a string it sends, for a packet whose id's low byte is pidLow.
	uint8_t scrambleKey(size_t i, uint8_t pidLow);
	// XOR in place: unscrambles what the game sent, or scrambles (the same operation).
	void scramble(uint8_t* data, size_t size, uint8_t pidLow);

	// A scrambled string as the game sends it: [length:2][sum:2][length - 2 bytes], length counting the sum; the sum
	// is not checked (the fan server ignores it too). Reads at payload[offset]; on success *next = the offset after it.
	// The key restarts at 0 for each string.
	std::optional<std::string> readScrambledString(const std::vector<uint8_t>& payload, size_t offset, uint8_t pidLow,
		size_t* next = nullptr);

	// A string as the server sends it: [length:2][bytes], no terminator.
	void putString(std::vector<uint8_t>& out, const std::string& bytes);
	void putU16(std::vector<uint8_t>& out, uint16_t value);
	void putU32(std::vector<uint8_t>& out, uint32_t value);
	uint16_t getU16(const std::vector<uint8_t>& payload, size_t offset); // 0 past the end

	// The session number travels as 10 ASCII digits: (its first 4 digits + pid) and (its last 4 + pid), 5 digits each.
	// Session numbers are 8 decimal digits (10000000..99999999).
	std::string encodeSessionDigits(uint32_t session, uint16_t pid);
	std::optional<uint32_t> decodeSessionDigits(const uint8_t* digits, uint16_t pid); // reads 10 bytes
}
