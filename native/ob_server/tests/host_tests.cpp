// SPDX-License-Identifier: AGPL-3.0-or-later
// LocalHost: the built-in host as the app runs it (docs/plans/2026-10-28-local-play.md): the server on one address,
// its beacon announcing the ports it really got, the players in its lobby, and the host's Remove.
#include "beacon.h"
#include "host.h"
#include "net.h"

#include <chrono>
#include <string>
#include <thread>

using namespace ob::server;

void Check(bool ok, const char* what);

namespace
{
	template <typename F>
	bool within(int ms, F condition)
	{
		for (int t = 0; t < ms; t += 20)
		{
			if (condition())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return condition();
	}

	bool closedWithin(net::Socket s, int ms)
	{
		uint8_t byte;
		return within(ms, [&] { return net::receiveSome(s, &byte, 1) < 0; });
	}
}

void TestLocalHost()
{
	BeaconListener listener;
	std::string error;
	if (!listener.start(0, &error))
	{
		Check(false, "host: the beacon listener starts");
		return;
	}
	LocalHost host;
	HostOptions options;
	options.name = "Test host";
	options.address = "127.0.0.1";
	options.webPort = 0; // any free port: the beacon must announce the one it got
	options.lobbyPort = 0;
	options.relayPort = 0;
	options.beaconTargets = {"127.0.0.1"};
	options.beaconPort = listener.port();
	options.beaconIntervalMs = 100;
	if (!host.start(options, &error))
	{
		Check(false, ("host: starts (" + error + ")").c_str());
		listener.stop();
		return;
	}
	Check(host.running() && host.webPort() != 0 && host.lobbyPort() != 0 && host.relayPort() != 0,
		"host: running, with real ports");
	Check(within(2000, [&] {
		const auto found = listener.hosts();
		return found.size() == 1 && found[0].info.webPort == host.webPort() && found[0].info.lobbyPort == host.lobbyPort() &&
			   found[0].info.name == "Test host";
	}), "host: its beacon announces its name and the ports it really got");
	Check(host.players().empty(), "host: no players yet");

	const net::Socket player = net::connectTcp("127.0.0.1", host.lobbyPort());
	Check(within(2000, [&] { return host.players().size() == 1 && host.players()[0] == "127.0.0.1"; }),
		"host: a lobby connection is listed by its address");
	Check(within(2000, [&] {
		const auto found = listener.hosts();
		return found.size() == 1 && found[0].info.players == 1;
	}), "host: the beacon counts the player");

	host.removePlayer("127.0.0.1");
	Check(closedWithin(player, 2000), "host: Remove closes the player's connection");
	const net::Socket again = net::connectTcp("127.0.0.1", host.lobbyPort());
	Check(closedWithin(again, 1000), "host: a removed player cannot come back");
	Check(within(2000, [&] { return host.players().empty(); }), "host: the removed player is gone from the list");
	net::closeSocket(player);
	net::closeSocket(again);

	host.stop();
	Check(!host.running(), "host: stopped");
	Check(within(2000, [&] { return listener.hosts(300).empty(); }), "host: the beacon stops with the host");
	listener.stop();
}

// The Windows host finds its own address: a local IPv4 on an adapter with a gateway (not loopback, not a virtual
// adapter without one). A machine with no such network finds none and says so.
void TestFindLocalNetwork()
{
	std::string address;
	int prefix = 0;
	if (findLocalNetwork(address, prefix))
		Check(isLocalAddress(address) && address.rfind("127.", 0) != 0 && prefix >= 1 && prefix <= 32,
			"host: this PC's network is a local, non-loopback address with a prefix");
	else
		Check(address.empty(), "host: no local network found, no address given");
}
