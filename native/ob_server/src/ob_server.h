// SPDX-License-Identifier: AGPL-3.0-or-later
// ob_server: the whole built-in server (docs/plans/2026-10-27-ob-server.md): the login pages and the lobby sharing one
// account list (the relay joins in step D). Started by Host game in the app, or by ob_server.exe on a dedicated PC.
#pragma once

#include "accounts.h"
#include "lobby.h"
#include "relay.h"
#include "tls_server.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ob::server
{
	struct ServerConfig
	{
		// Where it listens: loopback = this device only; "" = every interface (hosting for other devices).
		std::string bindAddress = "127.0.0.1";
		// The relay address handed to the game. Default: the alias this device's game reaches loopback through (its
		// adapter's PS2X_NET_REMAP 192.0.2.2=127.0.0.1; the game cannot use 127.x, report entry 34). When hosting,
		// this device's LAN address as other players reach it.
		std::array<uint8_t, 4> hostAddress{192, 0, 2, 2};
		uint16_t webPort = 443;                          // 18443 on an Android host (plan P8)
		uint16_t lobbyPort = 8300;
		uint16_t relayPort = 8690;
		std::string messageOfTheDay;
		std::function<void(const std::string&)> log;
		bool trace = false; // the lobby logs every packet in and out (--trace)
	};

	class Server
	{
	public:
		bool start(const ServerConfig& config, std::string* error);
		void stop();
		// The host's Remove: the player's connections close everywhere and it stays out for this session.
		void removePlayer(const std::string& address);
		Accounts& accounts() { return m_accounts; }
		uint16_t webPort() const { return m_web.port(); }
		uint16_t lobbyPort() const { return m_lobby.port(); }
		uint16_t relayPort() const { return m_relay.port(); }
		const LobbyServer& lobby() const { return m_lobby; }
		const TlsWebServer& web() const { return m_web; }
		const RelayServer& relay() const { return m_relay; }

	private:
		Accounts m_accounts;
		TlsWebServer m_web;
		LobbyServer m_lobby;
		RelayServer m_relay;
	};
}
