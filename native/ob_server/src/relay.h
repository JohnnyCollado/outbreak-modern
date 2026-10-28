// SPDX-License-Identifier: AGPL-3.0-or-later
// relay: the File #1 in-game relay on TCP 8690 (research/2026-10-27-lobby-protocol.md section 5). On connect the
// server asks for the session (GSLOGIN 0x1031, 12-byte header, who = relay server); the game answers with the session
// digits; the account's game number (set by the lobby at the start) decides who it plays with. After that the game
// sends frames [length:1 incl. itself][data] and each is copied unchanged to every other player of the same game. The
// server never reads game data. A broken stream (length 0) or a player silent too long is dropped; nobody is told.
#pragma once

#include "accounts.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace ob::server
{
	struct RelayConfig
	{
		int idleSeconds = 60;                         // drop a player silent this long
		std::function<void(const std::string&)> log; // one line per notable event; may be empty
	};

	class RelayServer
	{
	public:
		RelayServer();
		~RelayServer();
		RelayServer(const RelayServer&) = delete;
		RelayServer& operator=(const RelayServer&) = delete;

		bool start(Accounts& accounts, const RelayConfig& config, const std::string& address, uint16_t port,
			std::string* error);
		void stop();
		uint16_t port() const { return m_port; }
		uint32_t logins() const { return m_logins.load(); }
		uint64_t framesRelayed() const { return m_frames.load(); } // frames received and copied (once per frame)

		struct Impl;

	private:
		std::unique_ptr<Impl> m_impl;
		std::thread m_thread;
		std::atomic<bool> m_running{false};
		std::atomic<uint32_t> m_logins{0};
		std::atomic<uint64_t> m_frames{0};
		uint16_t m_port = 0;
	};
}
