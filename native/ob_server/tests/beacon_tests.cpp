// SPDX-License-Identifier: AGPL-3.0-or-later
// Nearby-game discovery (docs/plans/2026-10-28-local-play.md) and fuzzing of every parser that reads network input
// ("Network security" 4: random and truncated input must neither crash nor hang).
#include "beacon.h"
#include "protocol.h"
#include "web.h"

#include <chrono>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ob::server;

void Check(bool ok, const char* what);

static void TestBeaconFormat()
{
	BeaconInfo info{"Odin 3", 18443, 8300, 2};
	const std::string wire = encodeBeacon(info);
	Check(wire.size() <= kMaxBeaconBytes && wire.rfind("OBHOST1\n", 0) == 0, "beacon: magic first, at most 256 bytes");
	const auto back = decodeBeacon(reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
	Check(back && back->name == "Odin 3" && back->webPort == 18443 && back->lobbyPort == 8300 && back->players == 2,
		"beacon: decodes what it encoded");

	auto decodes = [](const std::string& text) {
		return decodeBeacon(reinterpret_cast<const uint8_t*>(text.data()), text.size()).has_value();
	};
	Check(!decodes("OBHOST2\nname=x\nweb=18443\nlobby=8300\nplayers=1\n"), "beacon: another version is ignored");
	Check(!decodes("obhost1\nname=x\nweb=18443\nlobby=8300\nplayers=1\n"), "beacon: the magic is exact");
	Check(!decodes("OBHOST1\nname=x\nweb=0\nlobby=8300\nplayers=1\n"), "beacon: port 0 is refused");
	Check(!decodes("OBHOST1\nname=x\nweb=70000\nlobby=8300\nplayers=1\n"), "beacon: a port above 65535 is refused");
	Check(!decodes("OBHOST1\nname=x\nweb=18443\nlobby=8300\nplayers=99\n"), "beacon: more than 64 players is refused");
	Check(!decodes("OBHOST1\nname=x\nweb=18443\nplayers=1\n"), "beacon: a missing field is refused");
	Check(!decodes("OBHOST1\nname=x\nweb=18443\nweb=1\nlobby=8300\nplayers=1\n"), "beacon: a repeated field is refused");
	Check(!decodes("OBHOST1\nname=x\nweb=18443\nlobby=8300\nplayers=1\nevil=1\n"), "beacon: an unknown field is refused");
	Check(!decodes(std::string("OBHOST1\nname=a\x01z\nweb=18443\nlobby=8300\nplayers=1\n")), "beacon: a control character in the name is refused");
	Check(!decodes("OBHOST1\nname=" + std::string(25, 'n') + "\nweb=18443\nlobby=8300\nplayers=1\n"), "beacon: a name over 24 characters is refused");
	Check(!decodes("OBHOST1\nname=x\nweb=18443\nlobby=8300\nplayers=1\n" + std::string(300, ' ')), "beacon: over 256 bytes is refused");
	Check(cleanHostName("  Odin\t3\x7f  ") == "Odin 3" && cleanHostName("") == "Host" &&
			  cleanHostName(std::string(40, 'a')).size() == 24,
		"beacon: host names are cleaned (printable, 24 characters, never empty)");
}

static void TestBeaconOverLoopback()
{
	BeaconListener listener;
	std::string error;
	if (!listener.start(0, &error))
	{
		Check(false, "beacon: the listener starts");
		return;
	}
	BeaconSender sender;
	if (!sender.start(BeaconInfo{"Test host", 18443, 8300, 1}, {"127.0.0.1"}, listener.port(), 100, &error))
	{
		Check(false, "beacon: the sender starts");
		listener.stop();
		return;
	}
	std::vector<FoundHost> hosts;
	for (int i = 0; i < 30 && hosts.empty(); i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		hosts = listener.hosts();
	}
	Check(hosts.size() == 1 && hosts[0].address == "127.0.0.1" && hosts[0].info.name == "Test host" &&
			  hosts[0].info.webPort == 18443 && hosts[0].info.players == 1,
		"beacon: a host on the network is found, with its address from the packet");
	sender.setPlayers(3);
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	hosts = listener.hosts();
	Check(hosts.size() == 1 && hosts[0].info.players == 3, "beacon: the player count follows the host");
	sender.stop();
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	Check(listener.hosts(300).empty(), "beacon: a host that stops announcing drops out of the list");
	listener.stop();
}

// Security queue S7: 32 fake hosts (any device on the network can send beacons) filled the list for good; entries
// that stop announcing now age out, so a real host is listed again.
static void TestBeaconListAgesOut()
{
	BeaconListener listener;
	std::string error;
	listener.setStaleMs(300);
	if (!listener.start(0, &error))
	{
		Check(false, "beacon aging: the listener starts");
		return;
	}
	const std::string fake = encodeBeacon(BeaconInfo{"Fake", 443, 8300, 0});
	for (int i = 2; i < 2 + int(kMaxListedHosts); i++)
	{
		const std::string from = "127.0.0." + std::to_string(i);
		const net::Socket s = net::openUdp(from, 0, false, nullptr, &error);
		if (s == net::kInvalid)
			continue;
		net::sendUdp(s, "127.0.0.1", listener.port(), reinterpret_cast<const uint8_t*>(fake.data()), fake.size());
		net::closeSocket(s);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	Check(listener.hosts().size() == kMaxListedHosts, "beacon aging: 32 fake hosts fill the list");
	// L2: while the 32 are still fresh, a new host takes the oldest place.
	{
		const net::Socket s = net::openUdp("127.0.0.50", 0, false, nullptr, &error);
		const std::string newcomer = encodeBeacon(BeaconInfo{"Newcomer", 443, 8300, 0});
		net::sendUdp(s, "127.0.0.1", listener.port(), reinterpret_cast<const uint8_t*>(newcomer.data()), newcomer.size());
		net::closeSocket(s);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		bool listed = false;
		for (const FoundHost& h : listener.hosts())
			listed |= h.info.name == "Newcomer";
		Check(listed && listener.hosts().size() == kMaxListedHosts, "beacon aging: a full list gives its oldest place to a new host");
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(400)); // they go quiet past the stale time
	BeaconSender real;
	real.start(BeaconInfo{"Real host", 18443, 8300, 1}, {"127.0.0.1"}, listener.port(), 100, &error);
	bool listed = false;
	for (int i = 0; i < 20 && !listed; i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		for (const FoundHost& h : listener.hosts(300))
			listed |= h.info.name == "Real host";
	}
	Check(listed, "beacon aging: a real host is listed once the fakes go quiet");
	real.stop();
	listener.stop();
}

// Random and truncated input to every parser of network bytes: no crash, no hang, and what comes out is bounded.
static void TestFuzz()
{
	std::mt19937 rng(20261028);
	std::uniform_int_distribution<int> byte(0, 255), len(0, 600);
	bool beaconOk = true, webOk = true, lobbyOk = true, relayOk = true;
	const std::string good = encodeBeacon(BeaconInfo{"Fuzz", 18443, 8300, 4});
	for (int round = 0; round < 20000; round++)
	{
		std::vector<uint8_t> data(static_cast<size_t>(len(rng)));
		for (uint8_t& b : data)
			b = static_cast<uint8_t>(byte(rng));
		if (round % 4 == 1 && !good.empty()) // mutate a valid beacon: one byte changed, or cut short
		{
			data.assign(good.begin(), good.end());
			if (round % 8 == 1)
				data[static_cast<size_t>(rng() % data.size())] = static_cast<uint8_t>(byte(rng));
			else
				data.resize(static_cast<size_t>(rng() % data.size()));
		}
		if (const auto b = decodeBeacon(data.data(), data.size()))
			beaconOk = beaconOk && b->name.size() <= 24 && b->webPort != 0 && b->lobbyPort != 0 && b->players <= 64;
		const std::string path = webRequestPath(std::string(data.begin(), data.end()));
		webOk = webOk && path.size() <= data.size();
		LobbyStream lobby;
		lobby.feed(data.data(), data.size());
		int packets = 0;
		while (lobby.next() && packets < 1000)
			packets++;
		lobbyOk = lobbyOk && packets < 1000;
		RelayStream relay;
		relay.feed(data.data(), data.size());
		int frames = 0;
		while ((relay.nextLogin() || relay.nextFrame()) && frames < 1000)
			frames++;
		relayOk = relayOk && frames < 1000;
	}
	Check(beaconOk, "fuzz: beacon decoding stays within its limits");
	Check(webOk, "fuzz: web request paths stay within the request");
	Check(lobbyOk, "fuzz: the lobby stream never yields endless packets");
	Check(relayOk, "fuzz: the relay stream never yields endless frames");
}

void TestBeacon()
{
	TestBeaconFormat();
	TestBeaconOverLoopback();
	TestBeaconListAgesOut();
	TestFuzz();
}
