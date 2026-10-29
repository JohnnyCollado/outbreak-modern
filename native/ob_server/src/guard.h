// SPDX-License-Identifier: AGPL-3.0-or-later
// guard: who may connect (docs/plans/2026-10-28-local-play.md, "Network security"). The server speaks the PS2's 2003
// protocols (SSL 3.0 / TLS 1.0, no real login), so it is only for the local network: private and loopback IPv4
// addresses, a cap per service and per address, and players the host removed stay out for the session.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace ob::server
{
	// Dotted IPv4 in 10/8, 172.16/12, 192.168/16, 169.254/16 or 127/8. Anything else (public, multicast, broadcast,
	// malformed, IPv6) is not local.
	bool isLocalAddress(const std::string& address);
	// Strict dotted quad (four parts 0-255, digits and dots only): the bytes, or false.
	bool parseIpv4(const std::string& text, std::array<uint8_t, 4>& out);

	struct GuardLimits
	{
		size_t total = 16;     // connections at once to one service
		size_t perAddress = 4; // of them from one address
		// Security queue S10: the host's own network (its Wi-Fi address and prefix length); prefix 0 = any local address.
		// This device (loopback) is always admitted.
		std::string subnet;
		int prefix = 0;
	};
	// address and subnet (dotted IPv4) share their first prefix bits (1..32); false for anything malformed.
	bool inSubnet(const std::string& address, const std::string& subnet, int prefix);

	// Thread-safe: services accept on their own threads, the host's Remove comes from the UI.
	class ConnectionGuard
	{
	public:
		explicit ConnectionGuard(GuardLimits limits = {}) : m_limits(limits) {}
		void setLimits(GuardLimits limits);
		// True and counted when the address may connect now; release() it when the connection closes.
		bool admit(const std::string& address);
		void release(const std::string& address);
		// The host removed this player: refused from now on (the caller closes its open connections).
		void remove(const std::string& address);
		bool removed(const std::string& address) const;

	private:
		mutable std::mutex m_mutex;
		GuardLimits m_limits;
		std::map<std::string, size_t> m_open;
		size_t m_total = 0;
		std::set<std::string> m_removed;
	};
}
