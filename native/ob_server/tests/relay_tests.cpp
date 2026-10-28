// SPDX-License-Identifier: AGPL-3.0-or-later
// The relay with scripted game clients on loopback (protocol spec section 5): the server asks each for its session
// (GSLOGIN), the client answers with the session digits, and frames are copied unchanged to the other players of the
// same game only; a length-0 frame drops its sender; an unknown session is disconnected.
#include "net.h"
#include "protocol.h"
#include "relay.h"

#include <chrono>
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
