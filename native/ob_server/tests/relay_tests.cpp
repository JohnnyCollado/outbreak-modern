// SPDX-License-Identifier: AGPL-3.0-or-later
// The relay with scripted game clients on loopback (protocol spec section 5): the server asks each for its session
// (GSLOGIN), the client answers with the session digits, and frames are copied unchanged to the other players of the
// same game only; a length-0 frame drops its sender; an unknown session is disconnected.
#include "net.h"
#include "protocol.h"
#include "relay.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ob::server;

void Check(bool ok, const char* what); // ob_server_tests.cpp

namespace
{
	struct RelayClient
	{
		net::Socket s = net::kInvalid;
		std::vector<uint8_t> in;
		bool closed = false;

		bool connect(uint16_t port)
		{
			s = net::connectTcp("127.0.0.1", port);
			return s != net::kInvalid;
		}
		~RelayClient() { net::closeSocket(s); }

		void pump(int ms)
		{
			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
			uint8_t buffer[4096];
			while (std::chrono::steady_clock::now() < until)
			{
				const long n = net::receiveSome(s, buffer, sizeof(buffer));
				if (n < 0)
				{
					closed = true;
					return;
				}
				if (n > 0)
					in.insert(in.end(), buffer, buffer + n);
				else
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}

		void sendBytes(const std::vector<uint8_t>& bytes)
		{
			size_t sent = 0;
			while (sent < bytes.size())
			{
				const long n = net::sendSome(s, bytes.data() + sent, bytes.size() - sent);
				if (n < 0)
					return;
				sent += static_cast<size_t>(n);
			}
		}

		// Reads the GSLOGIN question (12-byte header) and answers it with the session digits.
		bool login(uint32_t session)
		{
			for (int i = 0; i < 60 && in.size() < 12; i++)
				pump(50);
			if (in.size() < 12 || in[0] != 0x28 || in[1] != 0x01 || in[2] != 0x10 || in[3] != 0x31)
				return false;
			const uint16_t pid = static_cast<uint16_t>(in[6] << 8 | in[7]);
			in.erase(in.begin(), in.begin() + 12);
			Packet a;
			a.who = static_cast<uint8_t>(Who::RelayClient);
			a.kind = static_cast<uint8_t>(Kind::Tell);
			a.cmd = 0x1031;
			a.pid = pid;
			const std::string d = encodeSessionDigits(session, pid);
			a.payload.assign(d.begin(), d.end());
			sendBytes(encode(a));
			return true;
		}
	};
}

void TestRelay()
{
	Accounts accounts;
	const uint32_t sA = accounts.mintSession("A"), sB = accounts.mintSession("B"), sC = accounts.mintSession("C");
	accounts.setGameNumber("A", 2);
	accounts.setGameNumber("B", 2);
	accounts.setGameNumber("C", 3);
	RelayServer relay;
	RelayConfig config;
	config.sessionsByAddress = false; // several players from loopback, accounts named for them
	std::string error;
	Check(relay.start(accounts, config, "127.0.0.1", 0, &error) && relay.port() != 0, "the relay starts on loopback");

	RelayClient a, b, c;
	Check(a.connect(relay.port()) && b.connect(relay.port()) && c.connect(relay.port()), "three game clients connect");
	Check(a.login(sA) && b.login(sB) && c.login(sC), "each is asked GSLOGIN (28 01 10 31) and answers with its session");
	for (int i = 0; i < 40 && relay.logins() < 3; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	Check(relay.logins() == 3, "the relay counts three logins");

	const std::vector<uint8_t> frame = {5, 0x11, 0x22, 0x33, 0x44};
	a.sendBytes(frame);
	b.pump(300);
	a.pump(100);
	c.pump(100);
	Check(b.in == frame, "a frame from A reaches B, the other player of game 2, unchanged");
	Check(a.in.empty(), "the sender does not get its own frame back");
	Check(c.in.empty(), "a player of another game gets nothing");

	b.in.clear();
	const std::vector<uint8_t> two = {2, 0x01, 3, 0x02, 0x03};
	b.sendBytes(two);
	a.pump(300);
	Check(a.in == two, "two frames in one send arrive whole and in order");

	c.sendBytes({0}); // a length-0 frame: a broken stream
	c.pump(300);
	Check(c.closed, "a length-0 frame drops its sender");

	RelayClient d;
	Check(d.connect(relay.port()) && d.login(99999999u), "a client with an unknown session answers GSLOGIN");
	d.pump(300);
	Check(d.closed, "an unknown session is disconnected");
	Check(relay.framesRelayed() == 3, "three frames were relayed");
	relay.stop();
}

// The latency trace (reports/2026-10-26-online-play.md entry 64): with statsMs set, each logged-in player gets a line
// with the frames it sent in the window and its connection's round-trip time as the system measures it.
void TestRelayStats()
{
	Accounts accounts;
	const uint32_t sA = accounts.mintSession("A"), sB = accounts.mintSession("B");
	accounts.setGameNumber("A", 2);
	accounts.setGameNumber("B", 2);
	std::mutex logMutex;
	std::vector<std::string> lines;
	RelayServer relay;
	RelayConfig config;
	config.sessionsByAddress = false; // several players from loopback, accounts named for them
	config.statsMs = 300;
	config.log = [&](const std::string& l) {
		std::lock_guard<std::mutex> lock(logMutex);
		lines.push_back(l);
	};
	std::string error;
	Check(relay.start(accounts, config, "127.0.0.1", 0, &error), "stats: the relay starts");
	RelayClient a, b;
	Check(a.connect(relay.port()) && b.connect(relay.port()) && a.login(sA) && b.login(sB), "stats: two players log in");
	for (int i = 0; i < 40 && relay.logins() < 2; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	a.sendBytes({3, 0x11, 0x22}); // the length byte counts itself
	a.sendBytes({3, 0x33, 0x44});
	b.pump(300);
	bool found = false;
	for (int i = 0; i < 100 && !found; i++) // the next stats window (300 ms) after the frames
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		std::lock_guard<std::mutex> lock(logMutex);
		for (const std::string& l : lines)
			found |= l.find("127.0.0.1: 2 frames") != std::string::npos && l.find("round trip") != std::string::npos;
	}
	Check(found, "stats: a line per player with its frames and the round trip");
	relay.stop();
}

// Security review S2 and S4 (reports/2026-10-29-security-review.md): a connection that never logs in is closed after
// the login deadline, and a player who stops reading is dropped once its pending output passes the cap, instead of
// growing the host's memory without limit.
void TestRelayLimits()
{
	Accounts accounts;
	const uint32_t sA = accounts.mintSession("A"), sB = accounts.mintSession("B");
	accounts.setGameNumber("A", 2);
	accounts.setGameNumber("B", 2);
	std::mutex logMutex;
	std::vector<std::string> lines;
	RelayServer relay;
	RelayConfig config;
	config.sessionsByAddress = false; // several players from loopback, accounts named for them
	config.loginSeconds = 1;
	config.maxOutBytes = 64 * 1024;
	config.passOnBytesPerSecond = 0; // off here: this test floods on purpose to reach the receiver's cap
	config.log = [&](const std::string& l) {
		std::lock_guard<std::mutex> lock(logMutex);
		lines.push_back(l);
	};
	std::string error;
	Check(relay.start(accounts, config, "127.0.0.1", 0, &error), "limits: the relay starts");

	RelayClient silent;
	Check(silent.connect(relay.port()), "limits: a client connects and never logs in");
	silent.pump(3000);
	Check(silent.closed, "limits: it is closed after the login deadline");

	RelayClient a, b;
	Check(a.connect(relay.port()) && b.connect(relay.port()) && a.login(sA) && b.login(sB), "limits: two players log in");
	for (int i = 0; i < 40 && relay.logins() < 2; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	std::vector<uint8_t> frames;
	for (int i = 0; i < 64; i++) // 64 frames of 255 bytes per send: 16 KB
	{
		frames.push_back(255);
		frames.insert(frames.end(), 254, static_cast<uint8_t>(i));
	}
	bool dropped = false;
	for (int i = 0; i < 1024 && !dropped; i++) // up to 16 MB that B never reads
	{
		a.sendBytes(frames);
		std::lock_guard<std::mutex> lock(logMutex);
		for (const std::string& l : lines)
			dropped |= l.find("not reading") != std::string::npos;
	}
	Check(dropped, "limits: a player who stops reading is dropped at the output cap");
	relay.stop();
}

// Security review S6: a session works only from the address it was issued to (accounts are keyed by the device's
// address, ob_server.cpp quick login), so a sniffed or guessed number does not let another device act as its owner.
void TestSessionsByAddress()
{
	Accounts accounts;
	const uint32_t other = accounts.mintSession("10.0.0.9"), mine = accounts.mintSession("127.0.0.1");
	accounts.setGameNumber("10.0.0.9", 2);
	accounts.setGameNumber("127.0.0.1", 2);
	RelayServer relay;
	RelayConfig config;
	std::string error;
	Check(relay.start(accounts, config, "127.0.0.1", 0, &error), "by address: the relay starts");
	RelayClient thief, owner;
	Check(thief.connect(relay.port()) && thief.login(other), "by address: a client answers with another device's session");
	thief.pump(500);
	Check(thief.closed, "by address: the relay refuses a session from another address");
	Check(owner.connect(relay.port()) && owner.login(mine), "by address: a client answers with its own session");
	for (int i = 0; i < 40 && relay.logins() < 1; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	Check(relay.logins() == 1, "by address: its own session is accepted");
	relay.stop();
}

// Security queue 2, M3: a flooding sender is dropped by its own pass-on budget, so it can't get others kicked.
void TestRelaySenderBudget()
{
	Accounts accounts;
	const uint32_t sA = accounts.mintSession("A"), sB = accounts.mintSession("B");
	accounts.setGameNumber("A", 2);
	accounts.setGameNumber("B", 2);
	RelayServer relay;
	RelayConfig config;
	config.sessionsByAddress = false;
	config.passOnBytesPerSecond = 2000;
	std::string error;
	Check(relay.start(accounts, config, "127.0.0.1", 0, &error), "budget: the relay starts");
	RelayClient a, b;
	Check(a.connect(relay.port()) && b.connect(relay.port()) && a.login(sA) && b.login(sB), "budget: two log in");
	for (int i = 0; i < 40 && relay.logins() < 2; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	std::vector<uint8_t> frames;
	for (int i = 0; i < 64; i++)
	{
		frames.push_back(255);
		frames.insert(frames.end(), 254, 0x55);
	}
	a.sendBytes(frames);
	a.pump(1000);
	b.pump(300);
	Check(a.closed, "budget: the flooding sender is dropped");
	Check(!b.closed, "budget: the receiver stays");
	relay.stop();
}
