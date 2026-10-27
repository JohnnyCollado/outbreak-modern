// SPDX-License-Identifier: AGPL-3.0-or-later
// ob_server tests (ob.sh test): the wire format of research/2026-10-27-lobby-protocol.md section 3.
// - the 12-byte header round-trips, with FF FF FF in bytes 9..11 and the error flag in byte 8;
// - the lobby stream splits packets that arrive split or glued together;
// - the relay stream takes one header packet before login, then length-prefixed frames, and stops on length 0;
// - the scrambling decodes the version string in the fan server's packet capture (patch/update.txt: ciphertext
//   ae af a6 af b5 af a9 a0 a7 ac, packet id low byte 0x9B) into "0311172218", and round-trips;
// - the session number's 10 digits decode the capture's "2374125810" with packet id 0x4592 into 59318000, and
//   encode back.
#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ob::server;

static int s_fail = 0, s_total = 0;
static void Check(bool ok, const char* what)
{
	++s_total;
	if (!ok)
	{
		++s_fail;
		std::printf("FAIL: %s\n", what);
	}
}

static Packet MakePacket(uint16_t cmd, uint16_t pid, std::vector<uint8_t> payload)
{
	Packet p;
	p.who = static_cast<uint8_t>(Who::Server);
	p.kind = static_cast<uint8_t>(Kind::Query);
	p.cmd = cmd;
	p.pid = pid;
	p.payload = std::move(payload);
	return p;
}

static void TestHeader()
{
	const std::vector<uint8_t> bytes = encode(MakePacket(0x6101, 1, {0x28, 0x37}));
	const std::vector<uint8_t> want = {0x18, 0x01, 0x61, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0x28, 0x37};
	Check(bytes == want, "LOGIN query encodes as 18 01 61 01 00 02 00 01 00 FF FF FF 28 37");

	Packet err = MakePacket(0x6406, 7, {});
	err.error = true;
	Check(encode(err)[8] == 0xFF, "an error answer sets header byte 8 to FF");
}

static void TestLobbyStream()
{
	const std::vector<uint8_t> a = encode(MakePacket(0x6203, 3, {}));
	const std::vector<uint8_t> b = encode(MakePacket(0x6204, 4, {0x00, 0x01}));
	std::vector<uint8_t> both = a;
	both.insert(both.end(), b.begin(), b.end());

	LobbyStream s;
	s.feed(both.data(), 5); // split inside the first header
	Check(!s.next().has_value(), "no packet from a partial header");
	s.feed(both.data() + 5, both.size() - 5);
	const auto p1 = s.next();
	const auto p2 = s.next();
	Check(p1 && p1->cmd == 0x6203 && p1->pid == 3 && p1->payload.empty(), "first of two glued packets");
	Check(p2 && p2->cmd == 0x6204 && p2->payload == std::vector<uint8_t>({0x00, 0x01}), "second of two glued packets");
	Check(!s.next().has_value() && s.buffered() == 0, "nothing left over");
}

static void TestRelayStream()
{
	Packet login = MakePacket(0x1031, 9, std::vector<uint8_t>(10, '0'));
	login.who = static_cast<uint8_t>(Who::RelayClient);
	login.kind = static_cast<uint8_t>(Kind::Tell);
	std::vector<uint8_t> bytes = encode(login);
	const std::vector<uint8_t> frameA = {3, 0xAA, 0xBB}, frameB = {2, 0xCC};
	bytes.insert(bytes.end(), frameA.begin(), frameA.end());
	bytes.insert(bytes.end(), frameB.begin(), frameB.end());

	RelayStream s;
	s.feed(bytes.data(), bytes.size());
	Check(!s.nextFrame().has_value(), "no game frames before login");
	const auto l = s.nextLogin();
	Check(l && l->who == 0x82 && l->cmd == 0x1031 && l->payload.size() == 10, "the GSLOGIN answer comes out first");
	s.setLoggedIn();
	Check(!s.nextLogin().has_value(), "no login packets after login");
	const auto f1 = s.nextFrame();
	const auto f2 = s.nextFrame();
	Check(f1 && *f1 == frameA && f2 && *f2 == frameB, "game frames come out whole, length byte included");

	const uint8_t partial[] = {4, 1};
	s.feed(partial, 2);
	Check(!s.nextFrame().has_value() && !s.broken(), "a partial frame waits");
	const uint8_t rest[] = {2, 3, 0, 9};
	s.feed(rest, sizeof(rest));
	const auto f3 = s.nextFrame();
	Check(f3 && f3->size() == 4, "the partial frame completes");
	Check(!s.nextFrame().has_value() && s.broken(), "a length-0 frame breaks the stream");
}

static void TestScramble()
{
	const uint8_t capture[] = {0xae, 0xaf, 0xa6, 0xaf, 0xb5, 0xaf, 0xa9, 0xa0, 0xa7, 0xac};
	std::vector<uint8_t> text(capture, capture + sizeof(capture));
	scramble(text.data(), text.size(), 0x9B);
	Check(std::string(text.begin(), text.end()) == "0311172218", "the captured version string decodes to 0311172218");
	scramble(text.data(), text.size(), 0x9B);
	Check(std::memcmp(text.data(), capture, sizeof(capture)) == 0, "scrambling twice gives the original back");

	// [length:2][sum:2][data], length counting the sum, as the game sends a string.
	std::vector<uint8_t> payload = {0x00, 0x00}; // a 2-byte field before the string
	std::string handle = "******";
	std::vector<uint8_t> data(handle.begin(), handle.end());
	scramble(data.data(), data.size(), 0x10);
	putU16(payload, static_cast<uint16_t>(data.size() + 2));
	putU16(payload, 0x1234);
	payload.insert(payload.end(), data.begin(), data.end());
	size_t next = 0;
	const auto s = readScrambledString(payload, 2, 0x10, &next);
	Check(s && *s == "******" && next == payload.size(), "a scrambled string reads back with its end offset");
	(void)readScrambledString(payload, 3, 0x10); // a misaligned read must not crash (any result is fine)
	std::vector<uint8_t> shortPayload = {0x00, 0x20, 0x00, 0x00, 'a'};
	Check(!readScrambledString(shortPayload, 0, 0).has_value(), "a string longer than the payload is refused");
}

static void TestSession()
{
	const char* capture = "2374125810";
	const auto session = decodeSessionDigits(reinterpret_cast<const uint8_t*>(capture), 0x4592);
	Check(session && *session == 59318000u, "the captured digits with pid 0x4592 decode to session 59318000");
	Check(encodeSessionDigits(59318000u, 0x4592) == "2374125810", "and encode back to the same digits");
	Check(!decodeSessionDigits(reinterpret_cast<const uint8_t*>("23741X5810"), 0x4592).has_value(),
		"non-digits are refused");
	Check(!decodeSessionDigits(reinterpret_cast<const uint8_t*>("0000100001"), 0x4592).has_value(),
		"halves below the packet id are refused");
	for (uint32_t s : {10000000u, 99999999u, 12345678u})
		for (uint16_t pid : {uint16_t(1), uint16_t(0xFFFF)})
		{
			const std::string d = encodeSessionDigits(s, pid);
			const auto back = decodeSessionDigits(reinterpret_cast<const uint8_t*>(d.data()), pid);
			Check(d.size() == 10 && back && *back == s, "session digits round-trip at the extremes");
		}
}

int main()
{
	TestHeader();
	TestLobbyStream();
	TestRelayStream();
	TestScramble();
	TestSession();
	std::printf("ob_server: %d/%d\n", s_total - s_fail, s_total);
	return s_fail ? 1 : 0;
}
