#include "net_choice.h"

#include <array>
#include <cstdint>
#include <regex>

namespace obui
{
	namespace
	{
		bool Bytes(const std::string& s, std::array<int, 4>& out)
		{
			size_t part = 0, start = 0;
			for (size_t i = 0; i <= s.size(); i++)
			{
				if (i < s.size() && s[i] != '.')
				{
					if (s[i] < '0' || s[i] > '9')
						return false;
					continue;
				}
				const size_t len = i - start;
				if (part == 4 || len == 0 || len > 3 || (len > 1 && s[start] == '0'))
					return false;
				const int v = std::stoi(s.substr(start, len));
				if (v > 255)
					return false;
				out[part++] = v;
				start = i + 1;
			}
			return part == 4;
		}

		int Port(const std::string& s)
		{
			if (s.empty() || s.size() > 5)
				return 0;
			for (char c : s)
				if (c < '0' || c > '9')
					return 0;
			const int v = std::stoi(s);
			return v >= 1 && v <= 65535 ? v : 0;
		}

		const char* const kServerNames[] = {"www01.kddi-mmbb.jp", "gate1.jp.dnas.playstation.org"};
	} // namespace

	bool isIpv4(const std::string& s)
	{
		std::array<int, 4> b{};
		return Bytes(s, b);
	}

	bool isLocalIpv4(const std::string& s)
	{
		std::array<int, 4> b{};
		if (!Bytes(s, b))
			return false;
		return b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168) ||
		       (b[0] == 169 && b[1] == 254);
	}

	std::string broadcastAddress(const std::string& address, int prefix)
	{
		std::array<int, 4> b{};
		if (!Bytes(address, b) || prefix < 1 || prefix > 32)
			return "";
		const uint32_t ip = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
		const uint32_t mask = prefix == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> prefix);
		const uint32_t bc = (ip & mask) | ~mask;
		return std::to_string(bc >> 24) + "." + std::to_string((bc >> 16) & 0xFF) + "." + std::to_string((bc >> 8) & 0xFF) +
		       "." + std::to_string(bc & 0xFF);
	}

	std::string cleanHostName(const std::string& name)
	{
		std::string out;
		for (char c : name)
		{
			if (c == '\t')
				c = ' ';
			if (c >= 0x20 && c <= 0x7E)
				out.push_back(c);
		}
		const size_t first = out.find_first_not_of(' ');
		if (first == std::string::npos)
			return "Host";
		out = out.substr(first, out.find_last_not_of(' ') - first + 1);
		if (out.size() > 24)
			out.resize(24);
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		return out;
	}

	std::string netcnfValue(const NetConnection& c)
	{
		if (c.dhcp)
			return "dhcp";
		for (const std::string* f : {&c.address, &c.netmask, &c.gateway, &c.dns1})
			if (!isIpv4(*f))
				return "";
		if (!c.dns2.empty() && !isIpv4(c.dns2))
			return "";
		std::string v = "static:" + c.address + "," + c.netmask + "," + c.gateway + "," + c.dns1;
		if (!c.dns2.empty())
			v += "," + c.dns2;
		return v;
	}

	EnvList gameNetEnv(const std::string& server, int webPort, const std::string& iface, const NetConnection& c)
	{
		if (!isIpv4(server) || webPort < 1 || webPort > 65535)
			return {};
		static const std::regex kInterface("[A-Za-z0-9_.]{1,15}");
		if (!iface.empty() && !std::regex_match(iface, kInterface))
			return {};
		const std::string netcnf = netcnfValue(c);
		if (netcnf.empty())
			return {};
		EnvList env = {{"PS2X_DEV9", "pcsx2"}};
		if (!iface.empty())
			env.emplace_back("PS2X_NET_IF", iface);
		std::string hosts;
		for (const char* name : kServerNames)
			hosts += (hosts.empty() ? "" : ";") + std::string(name) + "=" + server;
		env.emplace_back("PS2X_NET_HOSTS", hosts);
		if (webPort != 443)
			env.emplace_back("PS2X_NET_REMAP", server + ":443=" + server + ":" + std::to_string(webPort));
		env.emplace_back("PS2X_NETCNF", netcnf);
		return env;
	}

	EnvList fanServerEnv(const std::string& dns, const std::string& iface, const NetConnection& c)
	{
		// An address a server can have: not 0.x, loopback, multicast or broadcast (security queue 2, F1).
		std::array<int, 4> b{};
		if (!Bytes(dns, b) || b[0] == 0 || b[0] == 127 || b[0] >= 224)
			return {};
		static const std::regex kInterface("[A-Za-z0-9_.]{1,15}");
		if (!iface.empty() && !std::regex_match(iface, kInterface))
			return {};
		NetConnection settings = c;
		if (!c.dhcp)
		{
			settings.dns1 = dns;
			settings.dns2 = c.dns1;
		}
		const std::string netcnf = netcnfValue(settings);
		if (netcnf.empty())
			return {};
		EnvList env = {{"PS2X_DEV9", "pcsx2"}};
		if (!iface.empty())
			env.emplace_back("PS2X_NET_IF", iface);
		env.emplace_back("PS2X_NET_DNS", dns);
		env.emplace_back("PS2X_NETCNF", netcnf);
		return env;
	}

	EnvList chosenNetEnv(const std::function<std::string(const std::string&)>& ui, const std::string& iface)
	{
		NetConnection c;
		if (ui("net_connection") == "manual")
			c = NetConnection{false, ui("net_address"), ui("net_netmask"), ui("net_gateway"), ui("net_dns1"), ui("net_dns2")};
		const std::string mode = ui("net_mode");
		if (mode == "local")
		{
			const std::string join = ui("net_join");
			const size_t colon = join.rfind(':');
			if (colon == std::string::npos || colon == 0)
				return {};
			const std::string address = join.substr(0, colon);
			const int port = Port(join.substr(colon + 1));
			if (!isLocalIpv4(address) || !port)
				return {};
			return gameNetEnv(address, port, iface, c);
		}
		if (mode == "server")
			return fanServerEnv(ui("net_server"), iface, c);
		return {};
	}
} // namespace obui
