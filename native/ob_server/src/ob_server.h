// SPDX-License-Identifier: AGPL-3.0-or-later
// ob_server: the whole built-in server (docs/plans/2026-10-27-ob-server.md): the login pages and the lobby sharing one
// account list (the relay joins in step D). Started by Host game in the app, or by ob_server.exe on a dedicated PC.
#pragma once

#include "accounts.h"
#include "lobby.h"
#include "tls_server.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ob::server
{
	struct ServerConfig
	{
		std::string bindAddress;                         // "" = every interface
		std::array<uint8_t, 4> hostAddress{127, 0, 0, 1}; // this device's LAN address, handed to the game for the relay
		uint16_t webPort = 443;                          // 18443 on an Android host (plan P8)
		uint16_t lobbyPort = 8300;
		uint16_t relayPort = 8690;
		std::string messageOfTheDay;
		std::function<void(const std::string&)> log;
	};

	class Server
	{
	public:
		bool start(const ServerConfig& config, std::string* error);
		void stop();
		Accounts& accounts() { return m_accounts; }
		uint16_t webPort() const { return m_web.port(); }
		uint16_t lobbyPort() const { return m_lobby.port(); }
		const LobbyServer& lobby() const { return m_lobby; }
		const TlsWebServer& web() const { return m_web; }

	private:
		Accounts m_accounts;
		TlsWebServer m_web;
		LobbyServer m_lobby;
	};
}
