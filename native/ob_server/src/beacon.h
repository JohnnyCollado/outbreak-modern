// SPDX-License-Identifier: AGPL-3.0-or-later
// beacon: nearby-game discovery for local play (docs/plans/2026-10-28-local-play.md). A host announces itself once a
// second with a UDP broadcast on port 18444; clients listen and list the hosts (as DS Download Play lists nearby
// games). The message is small fixed text read strictly; the host's address is taken from the packet as delivered,
// never from the text; only private and loopback senders are listed (guard.h).
//
//   OBHOST1\n name=<1-24 printable ASCII>\n web=<1-65535>\n lobby=<1-65535>\n players=<0-64>\n   (at most 256 bytes)
#pragma once

#include "net.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ob::server
{
	constexpr uint16_t kBeaconPort = 18444;
	constexpr size_t kMaxBeaconBytes = 256;
	constexpr size_t kMaxHostName = 24;
	constexpr size_t kMaxListedHosts = 32;

	struct BeaconInfo
	{
		std::string name;
		uint16_t webPort = 0;   // the login site (18443 on an Android host, 443 on a PC)
		uint16_t lobbyPort = 0; // 8300
		uint8_t players = 0;
	};

	// Printable ASCII only (other characters dropped, tabs become spaces), trimmed, at most 24 characters; "Host" when
	// nothing is left.
	std::string cleanHostName(const std::string& name);
	std::string encodeBeacon(const BeaconInfo& info);
	std::optional<BeaconInfo> decodeBeacon(const uint8_t* data, size_t size);

	// Announces one host until stopped. targets: the broadcast addresses to send to (255.255.255.255 and the Wi-Fi
	// subnet's; "127.0.0.1" in tests).
	class BeaconSender
	{
	public:
		~BeaconSender() { stop(); }
		bool start(const BeaconInfo& info, const std::vector<std::string>& targets, uint16_t port, int intervalMs,
			std::string* error);
		void setPlayers(uint8_t players);
		void stop();

	private:
		std::mutex m_mutex;
		BeaconInfo m_info;
		std::atomic<bool> m_running{false};
		std::thread m_thread;
		net::Socket m_socket = net::kInvalid;
	};

	struct FoundHost
	{
		std::string address; // as the network delivered the beacon
		BeaconInfo info;
	};

	// Listens for hosts until stopped; hosts() lists the ones heard recently.
	class BeaconListener
	{
	public:
		~BeaconListener() { stop(); }
		bool start(uint16_t port, std::string* error); // port 0 = any free port (tests)
		uint16_t port() const { return m_port; }
		std::vector<FoundHost> hosts(int maxAgeMs = 3000) const;
		// Entries silent this long are dropped before a new host is counted against the 32 cap (security queue S7).
		void setStaleMs(int ms) { m_staleMs = ms; }
		void stop();

	private:
		struct Seen
		{
			BeaconInfo info;
			std::chrono::steady_clock::time_point at;
		};
		mutable std::mutex m_mutex;
		std::map<std::string, Seen> m_seen;
		int m_staleMs = 10000;
		std::atomic<bool> m_running{false};
		std::thread m_thread;
		net::Socket m_socket = net::kInvalid;
		uint16_t m_port = 0;
	};
}
