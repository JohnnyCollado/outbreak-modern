// SPDX-License-Identifier: AGPL-3.0-or-later
#include "host.h"

#include "guard.h"

#include <algorithm>
#include <chrono>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <vector>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#endif

namespace ob::server
{
	bool findLocalNetwork(std::string& address, int& prefix)
	{
		address.clear();
		prefix = 0;
#if defined(_WIN32)
		ULONG size = 16 * 1024;
		std::vector<unsigned char> buffer;
		ULONG rc = ERROR_BUFFER_OVERFLOW;
		for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; tries++)
		{
			buffer.resize(size);
			rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
				nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
		}
		if (rc != NO_ERROR)
			return false;
		for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next)
		{
			if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || !a->FirstGatewayAddress)
				continue;
			for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
			{
				char text[INET_ADDRSTRLEN] = {};
				const auto* in = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
				if (!inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)) || !isLocalAddress(text) ||
					std::string(text).rfind("127.", 0) == 0)
					continue;
				address = text;
				prefix = u->OnLinkPrefixLength;
				return true;
			}
		}
		return false;
#else
		ifaddrs* list = nullptr;
		if (getifaddrs(&list) != 0)
			return false;
		for (ifaddrs* i = list; i; i = i->ifa_next)
		{
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask || !i->ifa_name)
				continue;
			const std::string name = i->ifa_name;
			if (name.rfind("wlan", 0) != 0 && name.rfind("eth", 0) != 0 && name.rfind("en", 0) != 0)
				continue;
			char text[INET_ADDRSTRLEN] = {};
			inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(i->ifa_addr)->sin_addr, text, sizeof(text));
			if (!isLocalAddress(text) || std::string(text).rfind("127.", 0) == 0)
				continue;
			const uint32_t mask = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_netmask)->sin_addr.s_addr);
			int bits = 0;
			for (uint32_t m = mask; m & 0x80000000u; m <<= 1)
				bits++;
			address = text;
			prefix = bits;
			break;
		}
		freeifaddrs(list);
		return !address.empty();
#endif
	}

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
		config.relayStatsMs = options.relayStatsMs;
		config.limits.subnet = options.address;
		config.limits.prefix = options.prefix;
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
				const size_t n = players().size();
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
		// In the lobby or in a game (on the relay); one address is one player.
		std::vector<std::string> all = m_server.lobby().connectedAddresses();
		for (const std::string& a : m_server.relay().connectedAddresses())
			if (std::find(all.begin(), all.end(), a) == all.end())
				all.push_back(a);
		return all;
	}

	void LocalHost::removePlayer(const std::string& address)
	{
		if (m_running)
			m_server.removePlayer(address);
	}
}
