// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ob_server.h"

namespace ob::server
{
	bool Server::start(const ServerConfig& config, std::string* error)
	{
		stop();
		LobbyConfig lobby;
		lobby.relayAddress = config.hostAddress;
		lobby.relayPort = config.relayPort;
		lobby.messageOfTheDay = config.messageOfTheDay;
		lobby.log = config.log;
		lobby.trace = config.trace;
		if (!m_lobby.start(m_accounts, lobby, config.bindAddress, config.lobbyPort, error))
			return false;

		WebConfig web;
		web.lobbyPort = config.lobbyPort;
		web.log = config.log;
		// Quick login (plan P12): one account per device. Until the app's hello (step E) names the device, its
		// network address does.
		web.mintSession = [this, log = config.log](const std::string& address) {
			const uint32_t session = m_accounts.mintSession(address);
			if (log)
				log("[web] " + address + ": quick login, session " + std::to_string(session));
			return session;
		};
		if (!m_web.start(web, config.bindAddress, config.webPort, error))
		{
			m_lobby.stop();
			return false;
		}
		return true;
	}

	void Server::stop()
	{
		m_web.stop();
		m_lobby.stop();
	}
}
