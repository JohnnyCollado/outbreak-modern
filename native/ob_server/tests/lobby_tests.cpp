// SPDX-License-Identifier: AGPL-3.0-or-later
// The lobby's minimal path with two scripted game clients on loopback (protocol spec section 4): log in with the
// session digits, version, new handle, character, area 1, room 1; the host creates game slot 1 for two players and
// opens it; the second player joins; the host starts; both get GETREADY, the same game number and the relay address.
// Plus: an unknown session is disconnected, an unknown question gets an empty answer.
#include "lobby.h"
#include "net.h"
#include "ob_server.h"
#include "protocol.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>
#include <vector>

using namespace ob::server;

void Check(bool ok, const char* what); // ob_server_tests.cpp

namespace
{
	struct GameClient
	{
		net::Socket s = net::kInvalid;
		LobbyStream in;
		std::deque<Packet> seen; // everything received, in order
		uint16_t pid = 100;

		bool connect(uint16_t port)
		{
			s = net::connectTcp("127.0.0.1", port);
			return s != net::kInvalid;
		}
		~GameClient() { net::closeSocket(s); }

		void sendPacket(Kind kind, uint16_t cmd, uint16_t id, const std::vector<uint8_t>& payload)
		{
			Packet p;
			p.who = static_cast<uint8_t>(Who::Client);
			p.kind = static_cast<uint8_t>(kind);
			p.cmd = cmd;
			p.pid = id;
			p.payload = payload;
			const auto bytes = encode(p);
			size_t sent = 0;
			while (sent < bytes.size())
			{
				const long n = net::sendSome(s, bytes.data() + sent, bytes.size() - sent);
				if (n < 0)
					return;
				sent += static_cast<size_t>(n);
			}
		}

		// The first not-yet-taken packet matching (cmd, kind), reading until it arrives or 3 s pass. Each packet is
		// taken once; a deque keeps the returned pointers valid while more arrive.
		const Packet* waitFor(uint16_t cmd, Kind kind)
		{
			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
			while (true)
			{
				for (size_t i = 0; i < seen.size(); i++)
					if (!taken[i] && seen[i].cmd == cmd && seen[i].kind == static_cast<uint8_t>(kind))
					{
						taken[i] = true;
						return &seen[i];
					}
				if (std::chrono::steady_clock::now() >= until)
					return nullptr;
				uint8_t buffer[4096];
				const long n = net::receiveSome(s, buffer, sizeof(buffer));
				if (n < 0)
					return nullptr;
				if (n == 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
					continue;
				}
				in.feed(buffer, static_cast<size_t>(n));
				while (auto p = in.next())
				{
					seen.push_back(*p);
					taken.push_back(false);
				}
			}
		}
		std::deque<bool> taken;

		// A question and its answer (same packet id).
		const Packet* ask(uint16_t cmd, const std::vector<uint8_t>& payload)
		{
			const uint16_t id = ++pid;
			sendPacket(Kind::Query, cmd, id, payload);
			for (const Packet* p; (p = waitFor(cmd, Kind::Tell)) != nullptr;)
				if (p->pid == id)
					return p;
			return nullptr;
		}

		// A string the way the game sends it: [length:2][sum:2][data scrambled with the packet id's low byte].
		static void putScrambled(std::vector<uint8_t>& out, const std::string& text, uint16_t id)
		{
			std::vector<uint8_t> data(text.begin(), text.end());
			scramble(data.data(), data.size(), static_cast<uint8_t>(id));
			putU16(out, static_cast<uint16_t>(data.size() + 2));
			putU16(out, 0);
			out.insert(out.end(), data.begin(), data.end());
		}

		// Log in as the game does; returns the handle the lobby gave us.
		std::string login(uint32_t session, const std::string& nickname)
		{
			const Packet* q = waitFor(0x6101, Kind::Query);
			if (!q)
				return "";
			std::vector<uint8_t> digits = {0x00, 0x0A};
			const std::string d = encodeSessionDigits(session, q->pid);
			digits.insert(digits.end(), d.begin(), d.end());
			sendPacket(Kind::Tell, 0x6101, q->pid, digits);
			const Packet* v = waitFor(0x6103, Kind::Query);
			if (!v)
				return "";
			std::vector<uint8_t> version = {0, 0, 0};
			std::vector<uint8_t> scrambled;
			putScrambled(scrambled, "0311172218", v->pid);
			version.insert(version.end(), scrambled.begin(), scrambled.end());
			sendPacket(Kind::Tell, 0x6103, v->pid, version);
			if (!waitFor(0x6131, Kind::Broadcast))
				return "";
			const uint16_t id = static_cast<uint16_t>(pid + 1);
			std::vector<uint8_t> pair;
			putScrambled(pair, "******", id);
			putScrambled(pair, nickname, id);
			const Packet* h = ask(0x6132, pair);
			if (!h || h->payload.size() != 8 || !waitFor(0x6104, Kind::Broadcast))
				return "";
			std::vector<uint8_t> stats = {0, 0, 0, 0};
			std::vector<uint8_t> raw(0xD0, 0x11);
			const uint16_t sid = static_cast<uint16_t>(pid + 1);
			scramble(raw.data(), raw.size(), static_cast<uint8_t>(sid));
			stats.insert(stats.end(), raw.begin(), raw.end());
			if (!ask(0x6190, stats))
				return "";
			return std::string(h->payload.begin() + 2, h->payload.end());
		}
	};

	std::vector<uint8_t> u16(uint16_t v)
	{
		std::vector<uint8_t> out;
		putU16(out, v);
		return out;
	}
} // namespace

void TestLobby()
{
	Accounts accounts;
	const uint32_t sessionA = accounts.mintSession("device-A");
	const uint32_t sessionB = accounts.mintSession("device-B");
	LobbyServer lobby;
	LobbyConfig config;
	config.relayAddress = {192, 168, 1, 20};
	std::string error;
	Check(lobby.start(accounts, config, "127.0.0.1", 0, &error) && lobby.port() != 0, "the lobby starts on loopback");

	GameClient a, b;
	Check(a.connect(lobby.port()) && b.connect(lobby.port()), "two game clients connect");
	const std::string handleA = a.login(sessionA, "HOST");
	const std::string handleB = b.login(sessionB, "GUEST");
	Check(handleA.size() == 6 && handleB.size() == 6 && handleA != handleB, "both log in and get new 6-character handles");
	Check(lobby.logins() == 2, "the lobby counts two logins");

	// The host: area 1, room 1, create slot 1 for two players, name it, open it.
	const Packet* areas = a.ask(0x6203, {});
	Check(areas && areas->payload == u16(2), "two areas");
	const Packet* areaSel = a.ask(0x6207, u16(1));
	Check(areaSel && areaSel->payload == u16(1), "area 1 selected");
	Check(a.ask(0x6305, u16(1)) != nullptr, "room 1 entered");
	const Packet* create = a.ask(0x6407, u16(1));
	Check(create && create->payload == std::vector<uint8_t>({0, 1}), "game slot 1 created");
	Check(a.ask(0x6509, {0, 0x11, 0, 1}) != nullptr, "scenario chosen");
	std::vector<uint8_t> name;
	GameClient::putScrambled(name, "test game", static_cast<uint16_t>(a.pid + 1));
	Check(a.ask(0x6609, name) != nullptr, "game named");
	Check(a.ask(0x660B, {0, 0}) != nullptr, "rule: two players");
	Check(a.ask(0x6504, {1}) != nullptr, "the host opens the game");

	// The guest sees it open and joins.
	b.ask(0x6207, u16(1));
	b.ask(0x6305, u16(1));
	const Packet* status = b.ask(0x6404, u16(1));
	Check(status && status->payload.size() == 3 && status->payload[2] == 3, "the guest sees slot 1 open (status 3)");
	const Packet* title = b.ask(0x6402, u16(1));
	Check(title && std::string(title->payload.begin() + 4, title->payload.end()) == "test game", "and its name");
	const Packet* join = b.ask(0x6406, u16(1));
	Check(join && !join->error && join->payload == std::vector<uint8_t>({0, 1}), "the guest joins slot 1");
	Check(a.waitFor(0x6503, Kind::Broadcast) != nullptr, "the host is told a player joined");

	// EVENTDAT 0x670D (report entry 51): the host's game sends a message to the new player by handle right after the
	// join, and the joiner waits for it on its member list. The request: two scrambled strings, the recipient's
	// handle and the event (each [length:2][sum:2][bytes], the key restarting at 0); the server answers the sender
	// with the recipient's handle and passes [sender's handle][event], both decoded as [length:2][bytes], to that
	// player alone as EVENTDATBC 0x670E (Bioserver1 Packet.getEvenData + sendEventDat, read 2026-10-28).
	const std::string event("\x01\x02\x7C\x7E\x10\x20\x30\x40", 8);
	std::vector<uint8_t> eventReq;
	GameClient::putScrambled(eventReq, handleB, static_cast<uint16_t>(a.pid + 1));
	GameClient::putScrambled(eventReq, event, static_cast<uint16_t>(a.pid + 1));
	const Packet* eventAns = a.ask(0x670D, eventReq);
	std::vector<uint8_t> toB;
	putString(toB, handleB);
	Check(eventAns && !eventAns->error && eventAns->payload == toB, "EVENTDAT is answered with the recipient's handle");
	const Packet* eventIn = b.waitFor(0x670E, Kind::Broadcast);
	std::vector<uint8_t> fromA;
	putString(fromA, handleA);
	putString(fromA, event);
	Check(eventIn && eventIn->payload == fromA,
		"the recipient gets EVENTDATBC: the sender's handle and the event, decoded");
	const Packet* full = b.ask(0x6404, u16(1));
	Check(full && full->payload.size() == 3 && full->payload[2] == 4, "two of two players: the slot is full");

	// Start.
	a.sendPacket(Kind::Broadcast, 0x6508, ++a.pid, {});
	Check(a.waitFor(0x6910, Kind::Broadcast) && b.waitFor(0x6910, Kind::Broadcast), "both players get GETREADY");
	Check(lobby.gamesStarted() == 1, "one game started");

	const Packet* gameA = a.ask(0x6915, {});
	const Packet* gameB = b.ask(0x6915, {});
	Check(gameA && gameB && gameA->payload == gameB->payload && gameA->payload.size() == 19 &&
			  std::string(gameA->payload.begin() + 2, gameA->payload.begin() + 17) == "000000000000002",
		"both get game number 2 as 15 digits");
	const Packet* relay = b.ask(0x6916, {});
	Check(relay && relay->payload == std::vector<uint8_t>({0, 4, 192, 168, 1, 20, 0, 2, 0x21, 0xF2, 0, 0, 0x1E, 0}),
		"the relay address is the host's, port 8690");
	const Packet* number = b.ask(0x6912, {});
	Check(number && number->payload == std::vector<uint8_t>({2}), "the guest is player 2");
	const Packet* hostStat = b.ask(0x6913, {1});
	Check(hostStat && hostStat->payload.size() > 12 && hostStat->payload[0] == 1 &&
			  std::string(hostStat->payload.begin() + 4, hostStat->payload.begin() + 10) == handleA,
		"player 1's record carries the host's handle");
	Check(hostStat && hostStat->payload.size() == 2 + 8 + 2 + 4 + 2 + 0xD0 + 3 && hostStat->payload[16 + 2] == 0x11,
		"and the host's character data, unscrambled");
	Check(accounts.gameNumber("device-B") == 2, "the relay will find the guest's game number");

	const Packet* unknown = a.ask(0x6181, {1, 2, 3});
	Check(unknown && unknown->payload.empty(), "an unknown question gets an empty answer");

	GameClient stranger;
	stranger.connect(lobby.port());
	const Packet* q = stranger.waitFor(0x6101, Kind::Query);
	std::vector<uint8_t> bad = {0, 0x0A};
	const std::string d = encodeSessionDigits(11112222u, q ? q->pid : 0);
	bad.insert(bad.end(), d.begin(), d.end());
	stranger.sendPacket(Kind::Tell, 0x6101, q ? q->pid : 0, bad);
	Check(q && stranger.waitFor(0x6103, Kind::Query) == nullptr, "an unknown session gets no further");
	lobby.stop();
}

std::string FetchTls(uint16_t port, int minor, int suite, const char* path); // ob_server_tests.cpp

// The whole chain on loopback: quick login over TLS hands out a session, and the lobby accepts it.
void TestServerChain()
{
	// Defaults: only this device (loopback) unless hosting is asked for, and the relay address handed to the game is
	// the alias its adapter maps to loopback (the game cannot use 127.x: report entry 34; upstream/pcsx2 0003).
	{
		const ServerConfig defaults;
		Check(defaults.bindAddress == "127.0.0.1", "the server listens on loopback only by default");
		Check(defaults.hostAddress == std::array<uint8_t, 4>{192, 0, 2, 2},
			"the relay address handed out by default is the alias 192.0.2.2");
	}
	Server server;
	ServerConfig config;
	config.bindAddress = "127.0.0.1";
	config.webPort = 0;
	config.lobbyPort = 0;
	std::string error;
	Check(server.start(config, &error), "the server (login pages + lobby) starts");
	const std::string top = FetchTls(server.webPort(), 1, 0x16 /* DHE-RSA-3DES */, "/00000002/CRS-top.jsp");
	const size_t at = top.find("sessid=");
	const uint32_t session = at == std::string::npos ? 0 : static_cast<uint32_t>(std::stoul(top.substr(at + 7, 8)));
	Check(session >= 10000000u && server.accounts().accountForSession(session) == std::string("127.0.0.1"),
		"quick login mints a session for this device's address");
	GameClient c;
	Check(c.connect(server.lobbyPort()) && c.login(session, "PLAYER").size() == 6,
		"the lobby accepts the session the login page handed out");
	server.stop();
}
