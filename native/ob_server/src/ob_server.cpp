// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ob_server.h"

namespace ob::server
{
	bool Server::start(const ServerConfig& config, std::string* error)
	{
		stop();
		// The relay first, then the lobby and the login pages, each told the ports the others really got (port 0 =
		// any free port, as a host may ask).
		RelayConfig relay;
		relay.log = config.log;
		relay.statsMs = config.relayStatsMs;
		relay.limits = config.limits;
		if (!m_relay.start(m_accounts, relay, config.bindAddress, config.relayPort, error))
			return false;
		LobbyConfig lobby;
		lobby.relayAddress = config.hostAddress;
		lobby.relayPort = m_relay.port();
		lobby.messageOfTheDay = config.messageOfTheDay;
		lobby.log = config.log;
		lobby.trace = config.trace;
		lobby.limits = config.limits;
		if (!m_lobby.start(m_accounts, lobby, config.bindAddress, config.lobbyPort, error))
		{
			m_relay.stop();
			return false;
		}

		WebConfig web;
		web.lobbyPort = m_lobby.port();
		web.log = config.log;
		web.trace = config.trace;
		web.limits = config.limits;
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
			m_relay.stop();
			m_lobby.stop();
			return false;
		}
		return true;
	}

	void Server::removePlayer(const std::string& address)
	{
		m_lobby.removeAddress(address);
		m_relay.removeAddress(address);
		m_web.removeAddress(address);
	}

	void Server::stop()
	{
		m_web.stop();
		m_relay.stop();
		m_lobby.stop();
	}
}
