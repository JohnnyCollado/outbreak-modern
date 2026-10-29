// SPDX-License-Identifier: AGPL-3.0-or-later
// host: the built-in host as the app runs it (docs/plans/2026-10-28-local-play.md): the server bound to this device's
// Wi-Fi address only (never every interface), its beacon announcing the ports it really got, the players in its lobby,
// and the host's Remove. Android starts it from a foreground service; Windows from the game.
#pragma once

#include "beacon.h"
#include "ob_server.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace ob::server
{
	struct HostOptions
	{
		std::string name;    // shown to nearby players (cleaned: printable, 24 characters)
		std::string address; // this device's Wi-Fi IPv4: must be a local address (guard.h)
		uint16_t webPort = 18443;  // login site: 18443 on Android (no port below 1024 there), 443 on a PC
		uint16_t lobbyPort = 8300;
		uint16_t relayPort = 8690;
		std::vector<std::string> beaconTargets{"255.255.255.255"}; // and the Wi-Fi subnet's broadcast address
		uint16_t beaconPort = kBeaconPort;
		int beaconIntervalMs = 1000;
		std::function<void(const std::string&)> log;
	};

	class LocalHost
	{
	public:
		~LocalHost() { stop(); }
		bool start(const HostOptions& options, std::string* error);
		void stop();
		bool running() const { return m_running; }
		uint16_t webPort() const { return m_server.webPort(); }
		uint16_t lobbyPort() const { return m_server.lobbyPort(); }
		uint16_t relayPort() const { return m_server.relayPort(); }
		// The addresses in the lobby now.
		std::vector<std::string> players() const;
		void removePlayer(const std::string& address);

	private:
		Server m_server;
		BeaconSender m_beacon;
		std::atomic<bool> m_running{false};
		std::thread m_counter; // keeps the beacon's player count current
	};
}
