// SPDX-License-Identifier: AGPL-3.0-or-later
// ob_server.exe: the built-in server as a console program for a dedicated PC (plan decision N6).
//
//   ob_server [--host a.b.c.d] [--bind a.b.c.d] [--web-port N] [--lobby-port N] [--relay-port N] [--trace]
//
// By default it serves this PC only: it listens on 127.0.0.1, and the game on this PC reaches it through the alias
// 192.0.2.2 (PS2X_NET_HOSTS=www01.kddi-mmbb.jp=192.0.2.2, PS2X_NET_REMAP=192.0.2.2=127.0.0.1: the game cannot use
// 127.x, reports/2026-10-26-online-play.md entry 34). To host for other devices: --bind "" (every interface) or
// --bind <LAN address>, and --host <LAN address> (handed to the game for the relay); their games point
// www01.kddi-mmbb.jp at that address. Ends with Ctrl+C.
#include "ob_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
	std::atomic<bool> s_stop{false};
	void onSignal(int) { s_stop = true; }

	bool parseAddress(const char* text, std::array<uint8_t, 4>* out)
	{
		unsigned a, b, c, d;
		char tail;
		if (std::sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || (a | b | c | d) > 255)
			return false;
		*out = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d)};
		return true;
	}
}

int main(int argc, char** argv)
{
	ob::server::ServerConfig config;
	std::mutex logMutex;
	config.log = [&](const std::string& line) {
		std::lock_guard<std::mutex> lock(logMutex);
		std::cout << line << std::endl;
	};
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
		if (arg == "--host" && value && parseAddress(value, &config.hostAddress))
			i++;
		else if (arg == "--bind" && value)
			config.bindAddress = value, i++;
		else if (arg == "--web-port" && value)
			config.webPort = static_cast<uint16_t>(std::atoi(value)), i++;
		else if (arg == "--lobby-port" && value)
			config.lobbyPort = static_cast<uint16_t>(std::atoi(value)), i++;
		else if (arg == "--relay-port" && value)
			config.relayPort = static_cast<uint16_t>(std::atoi(value)), i++;
		else if (arg == "--trace")
			config.trace = true;
		else
		{
			std::cerr << "usage: ob_server [--host a.b.c.d] [--bind a.b.c.d] [--web-port N] [--lobby-port N] "
						 "[--relay-port N] [--trace]\n";
			return 2;
		}
	}
	ob::server::Server server;
	std::string error;
	if (!server.start(config, &error))
	{
		std::cerr << "ob_server: " << error << "\n";
		return 1;
	}
	std::cout << "ob_server: login pages on " << server.webPort() << ", lobby on " << server.lobbyPort()
			  << ", relay address given to players " << int(config.hostAddress[0]) << "." << int(config.hostAddress[1])
			  << "." << int(config.hostAddress[2]) << "." << int(config.hostAddress[3]) << ":" << config.relayPort
			  << " (relay listening on " << server.relayPort() << "). Ctrl+C ends." << std::endl;
	std::signal(SIGINT, onSignal);
	while (!s_stop)
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	server.stop();
	return 0;
}
