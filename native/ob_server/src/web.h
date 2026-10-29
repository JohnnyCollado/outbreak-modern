// SPDX-License-Identifier: AGPL-3.0-or-later
// web: the login pages the game's own browser walks (research/2026-10-27-lobby-protocol.md section 2), with quick
// login (plan P12): CRS-top.jsp redirects straight to the game-start page carrying a session number minted for the
// player, so no ID or password is typed. Pages only; TLS and sockets are in tls_server.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ob::server
{
	struct WebConfig
	{
		std::string siteHost = "www01.kddi-mmbb.jp"; // the game's fixed login site; our adapter's DNS points it at us
		std::string lobbyHost = "www01.kddi-mmbb.jp"; // the lobby address handed to the game (a name: same DNS rule)
		uint16_t lobbyPort = 8300;
		// A new 8-digit session number (10000000..99999999) for the player at this address; the lobby later looks
		// it up. Set by the server (accounts); tests give their own.
		std::function<uint32_t(const std::string& clientAddress)> mintSession;
		// One line per connection event (accepted, handshake result); from the server's thread. Empty = none.
		std::function<void(const std::string& line)> log;
		// Also the client's first bytes after a successful handshake (a failed one always shows them).
		bool trace = false;
	};

	struct WebResponse
	{
		int status = 200;
		std::string contentType = "text/html";
		std::string body;
	};

	// The answer for one request (path as in the request line, query included).
	WebResponse webRespond(const WebConfig& config, const std::string& path, const std::string& clientAddress);

	// The bytes sent for it: HTTP/1.0 status line, Content-Type, exact Content-Length, Connection: close (the fan setup
	// forces HTTP/1.0 without keep-alive for the PS2's clients).
	std::string webSerialize(const WebResponse& response);

	// The request line's path from a raw HTTP request ("GET /x HTTP/1.0\r\n..."); empty if it isn't one.
	std::string webRequestPath(const std::string& request);
}
