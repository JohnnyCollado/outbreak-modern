// SPDX-License-Identifier: AGPL-3.0-or-later
// The local-network guard (docs/plans/2026-10-28-local-play.md, "Network security"): only private and loopback
// addresses, limits per service and per address, and a removed player stays out.
#include "guard.h"
#include "lobby.h"
#include "net.h"

#include <chrono>
#include <string>
#include <thread>

using namespace ob::server;

void Check(bool ok, const char* what);

static void TestLocalAddresses()
{
	for (const char* a : {"10.0.0.1", "10.255.255.255", "172.16.0.1", "172.31.255.254", "192.168.0.63", "169.254.10.20",
			 "127.0.0.1", "127.0.0.2"})
		Check(isLocalAddress(a), (std::string("local: ") + a).c_str());
	for (const char* a : {"8.8.8.8", "172.15.255.255", "172.32.0.1", "192.169.0.1", "100.64.0.1", "0.0.0.0",
			 "255.255.255.255", "224.0.0.1", "", "192.168.0", "192.168.0.256", "192.168.0.1.5", "a.b.c.d", "1.2.3.4x",
			 "::1", "fe80::1"})
		Check(!isLocalAddress(a), (std::string("not local: '") + a + "'").c_str());
}

static void TestGuardLimits()
{
	ConnectionGuard guard(GuardLimits{3, 2});
	Check(guard.admit("192.168.0.10") && guard.admit("192.168.0.10"), "guard: two from one address");
	Check(!guard.admit("192.168.0.10"), "guard: a third from the same address is refused");
	Check(guard.admit("192.168.0.11"), "guard: another address still fits");
	Check(!guard.admit("192.168.0.12"), "guard: the total is capped");
	guard.release("192.168.0.10");
	Check(guard.admit("192.168.0.12"), "guard: a released slot is free again");
	Check(!guard.admit("8.8.8.8"), "guard: a public address is refused");
	guard.remove("192.168.0.11");
	Check(!guard.admit("192.168.0.11"), "guard: a removed player stays out");
	Check(guard.removed("192.168.0.11") && !guard.removed("192.168.0.10"), "guard: removed() names only that address");
}

// The lobby enforces it at accept: with two per address, a third connection from 127.0.0.1 is closed at once.
static void TestLobbyRefusesBeyondLimit()
{
	Accounts accounts;
	LobbyServer lobby;
	LobbyConfig config;
	config.limits = GuardLimits{16, 2};
	std::string error;
	if (!lobby.start(accounts, config, "127.0.0.1", 0, &error))
	{
		Check(false, "guard: the lobby starts");
		return;
	}
	const net::Socket a = net::connectTcp("127.0.0.1", lobby.port());
	const net::Socket b = net::connectTcp("127.0.0.1", lobby.port());
	const net::Socket c = net::connectTcp("127.0.0.1", lobby.port());
	// The refused one reads end-of-stream (or an error) within a second; the admitted ones stay open.
	auto closedWithin = [](net::Socket s, int ms) {
		uint8_t byte;
		for (int t = 0; t < ms; t += 20)
		{
			const long got = net::receiveSome(s, &byte, 1);
			if (got < 0)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return false;
	};
	Check(closedWithin(c, 1000), "guard: the lobby closes a third connection from one address");
	Check(!closedWithin(a, 200) && !closedWithin(b, 200), "guard: the first two stay open");
	net::closeSocket(a);
	net::closeSocket(b);
	net::closeSocket(c);
	lobby.stop();
}

// Security queue S10: a host on a big shared network (a hotel's or campus's 10.x) accepts only its own subnet, not
// every private address; this device (loopback) still may.
static void TestGuardSubnet()
{
	GuardLimits limits;
	limits.subnet = "192.168.0.76";
	limits.prefix = 24;
	ConnectionGuard guard(limits);
	Check(guard.admit("192.168.0.9"), "subnet: an address on the host's /24 is admitted");
	Check(!guard.admit("192.168.1.5"), "subnet: another 192.168 network is refused");
	Check(!guard.admit("10.0.0.2"), "subnet: another private range is refused");
	Check(guard.admit("127.0.0.1"), "subnet: this device (loopback) is admitted");
	GuardLimits wide;
	wide.subnet = "10.20.30.40";
	wide.prefix = 8;
	ConnectionGuard campus(wide);
	Check(campus.admit("10.200.1.1") && !campus.admit("192.168.0.9"), "subnet: a /8 admits its range only");
	ConnectionGuard open;
	Check(open.admit("192.168.1.5") && open.admit("10.0.0.2"), "subnet: none set = any local address, as before");
}

void TestGuard()
{
	TestLocalAddresses();
	TestGuardSubnet();
	TestGuardLimits();
	TestLobbyRefusesBeyondLimit();
}
