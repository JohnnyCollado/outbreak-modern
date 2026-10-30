// SPDX-License-Identifier: AGPL-3.0-or-later
// lobby: the File #1 lobby on TCP 8300 (research/2026-10-27-lobby-protocol.md section 4): login with the session
// number, handle names, 2 areas x 10 rooms x 20 game slots, create/join/start, and the hand-off to the relay (game
// number + relay address). One thread; every client question gets an answer with its packet id, and a command not
// known here gets an empty answer and one log line, so the game never waits forever on us.
#pragma once

#include <vector>
#include "guard.h"
#include "accounts.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace ob::server
{
	struct LobbyConfig
	{
		std::array<uint8_t, 4> relayAddress{127, 0, 0, 1}; // handed to the game in GSINFO (the host's LAN address)
		uint16_t relayPort = 8690;
		std::string messageOfTheDay;                        // game markup; empty = none
		int heartbeatSeconds = 30;                          // HEARTBEAT broadcast; CONNCHECK at twice this
		std::function<void(const std::string&)> log;        // one line per notable event; may be empty
		bool trace = false;                                 // also one line per packet in and out (ob_server --trace)
		GuardLimits limits;                                 // local addresses only; per service and per address (guard.h)
		// Security review S2-S4 (reports/2026-10-29-security-review.md): a connection that has not logged in by then is
		// closed; a client whose pending output passes the cap (it stopped reading) is dropped; a dropped client is
		// removed once its last answer is sent, or after closeGraceMs.
		int loginSeconds = 15;
		size_t maxOutBytes = 256 * 1024;
		int closeGraceMs = 2000;
		// Security queue 2: log lines one client can cause (L1), and bytes a client may pass on to others per second
		// (M3): past it the sender is dropped, so a flood can't push others over their output cap. 0 = no limit.
		int clientLogLines = 200;
		size_t passOnBytesPerSecond = 64 * 1024;
		// Security review S6: a session is accepted only from the address its account is named for (quick login names
		// the account by the device's address, ob_server.cpp), so a sniffed or guessed number is useless elsewhere.
		bool sessionsByAddress = true;
	};

	class LobbyServer
	{
	public:
		LobbyServer();
		~LobbyServer();
		LobbyServer(const LobbyServer&) = delete;
		LobbyServer& operator=(const LobbyServer&) = delete;

		bool start(Accounts& accounts, const LobbyConfig& config, const std::string& address, uint16_t port,
			std::string* error);
		void stop();
		uint16_t port() const { return m_port; }
		uint32_t logins() const { return m_logins.load(); }
		// The addresses with a lobby connection now (one entry each), as of the server's last loop (~100 ms).
		std::vector<std::string> connectedAddresses() const;
		// The host removed this player: its connections close and it is refused for the rest of the session.
		void removeAddress(const std::string& address);
		uint32_t gamesStarted() const { return m_games.load(); }

		struct Impl;

	private:
		std::unique_ptr<Impl> m_impl;
		std::thread m_thread;
		std::atomic<bool> m_running{false};
		std::atomic<uint32_t> m_logins{0}, m_games{0};
		uint16_t m_port = 0;
	};
}
