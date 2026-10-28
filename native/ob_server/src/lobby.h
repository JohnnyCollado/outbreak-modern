// SPDX-License-Identifier: AGPL-3.0-or-later
// lobby: the File #1 lobby on TCP 8300 (research/2026-10-27-lobby-protocol.md section 4): login with the session
// number, handle names, 2 areas x 10 rooms x 20 game slots, create/join/start, and the hand-off to the relay (game
// number + relay address). One thread; every client question gets an answer with its packet id, and a command not
// known here gets an empty answer and one log line, so the game never waits forever on us.
#pragma once

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
