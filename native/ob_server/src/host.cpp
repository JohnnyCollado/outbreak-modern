// SPDX-License-Identifier: AGPL-3.0-or-later
#include "host.h"

#include "guard.h"

#include <chrono>

namespace ob::server
{
	bool LocalHost::start(const HostOptions& options, std::string* error)
	{
		stop();
		// Local network only: the server listens on this one address, which must itself be local.
		if (!isLocalAddress(options.address))
		{
			*error = "not a local network address: " + options.address;
			return false;
		}
		ServerConfig config;
		parseIpv4(options.address, config.hostAddress); // already checked by isLocalAddress
		config.bindAddress = options.address;
		config.webPort = options.webPort;
		config.lobbyPort = options.lobbyPort;
		config.relayPort = options.relayPort;
		config.log = options.log;
		if (!m_server.start(config, error))
			return false;
		BeaconInfo info{options.name, m_server.webPort(), m_server.lobbyPort(), 0};
		if (!m_beacon.start(info, options.beaconTargets, options.beaconPort, options.beaconIntervalMs, error))
		{
			m_server.stop();
			return false;
		}
		m_running = true;
		m_counter = std::thread([this] {
			while (m_running)
			{
				const size_t n = m_server.lobby().connectedAddresses().size();
				m_beacon.setPlayers(static_cast<uint8_t>(n > 64 ? 64 : n));
				for (int t = 0; m_running && t < 500; t += 20)
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		});
		if (options.log)
			options.log("[host] hosting \"" + cleanHostName(options.name) + "\" on " + options.address + " (login " +
						std::to_string(m_server.webPort()) + ", lobby " + std::to_string(m_server.lobbyPort()) + ", relay " +
						std::to_string(m_server.relayPort()) + ")");
		return true;
	}

	void LocalHost::stop()
	{
		m_running = false;
		if (m_counter.joinable())
			m_counter.join();
		m_beacon.stop();
		m_server.stop();
	}

	std::vector<std::string> LocalHost::players() const
	{
		return m_server.lobby().connectedAddresses();
	}

	void LocalHost::removePlayer(const std::string& address)
	{
		if (m_running)
			m_server.removePlayer(address);
	}
}
