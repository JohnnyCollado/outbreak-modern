// net_choice: the Network page's rules (docs/plans/2026-10-28-local-play.md), shared by the Windows menu; the Android
// launcher has the same rules in Kotlin (native/android/kotlin/.../NetPlay.kt) with the same test cases. What the game
// is told comes from here only, and every value is checked strictly first: the game's settings are ';'- and
// '='-separated lists (PS2X_NET_HOSTS, PS2X_NET_REMAP), so an unchecked value could add entries of its own.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace obui
{
	// Strict dotted quad: four parts 0-255, digits only, no leading zeros.
	bool isIpv4(const std::string& s);
	// On the local network: 10/8, 172.16/12, 192.168/16 or 169.254/16 (not loopback: another device is never 127.x).
	bool isLocalIpv4(const std::string& s);
	// The subnet's broadcast address ("" for a bad address or a prefix outside 1..32).
	std::string broadcastAddress(const std::string& address, int prefix);
	// As ob_server's cleanHostName: printable ASCII, trimmed, 24 at most, "Host" if nothing is left.
	std::string cleanHostName(const std::string& name);

	// The game's own network settings: DHCP, or typed in by the player.
	struct NetConnection
	{
		bool dhcp = true;
		std::string address, netmask, gateway, dns1, dns2;
	};
	// PS2X_NETCNF (PS2Recomp patch 0097): "dhcp" or "static:address,netmask,gateway,dns1[,dns2]"; "" if a field is bad.
	std::string netcnfValue(const NetConnection& c);

	using EnvList = std::vector<std::pair<std::string, std::string>>;
	// The game's environment for playing on server (IPv4: a local host or a fan server) whose login answers on webPort:
	// the game's server names to it, 443 remapped to webPort when it differs, the interface ("" = automatic) and the
	// game's settings. Empty if anything is unsound.
	EnvList gameNetEnv(const std::string& server, int webPort, const std::string& iface, const NetConnection& c);
	// A fan server, given by its DNS address as PCSX2 players set DNS1 (report entry 65): the game asks that DNS for
	// its login and DNAS names. DHCP hands it out (PS2X_NET_DNS); manual settings get it as DNS 1, their own DNS 1
	// second. Empty if anything is unsound.
	EnvList fanServerEnv(const std::string& dns, const std::string& iface, const NetConnection& c);
	// From the saved choices (ui reads outbreak.ini's [ui] keys, "" when unset): net_mode off | local (net_join =
	// "address:webPort", local addresses only) | server (net_server = the fan server's DNS); net_connection dhcp | manual
	// (net_address, net_netmask, net_gateway, net_dns1, net_dns2). Empty = the game plays offline.
	EnvList chosenNetEnv(const std::function<std::string(const std::string&)>& ui, const std::string& iface);
} // namespace obui
