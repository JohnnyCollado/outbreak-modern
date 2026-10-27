// SPDX-License-Identifier: AGPL-3.0-or-later
// net: the little socket layer the lobby and relay share: non-blocking TCP listen/accept/connect, send/receive, and
// a poll over many sockets. Windows (Winsock) and POSIX (Android/Linux).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ob::server::net
{
#ifdef _WIN32
	using Socket = uintptr_t;
#else
	using Socket = int;
#endif
	extern const Socket kInvalid;

	bool startup();                     // Winsock once per process; true elsewhere
	void closeSocket(Socket s);
	// Listens on address:port (address "" = all interfaces; port 0 = any free port). *boundPort = the port.
	Socket listenTcp(const std::string& address, uint16_t port, uint16_t* boundPort, std::string* error);
	Socket acceptTcp(Socket listener, std::string* peerAddress); // kInvalid if nothing waiting
	Socket connectTcp(const std::string& address, uint16_t port); // blocking connect, then non-blocking; for tests
	// Non-blocking send/receive: bytes moved; 0 = would block; -1 = closed or failed.
	long sendSome(Socket s, const uint8_t* data, size_t size);
	long receiveSome(Socket s, uint8_t* data, size_t size);

	struct PollEntry
	{
		Socket socket;
		bool wantWrite = false;
		bool readable = false; // out
		bool writable = false; // out
		bool failed = false;   // out: error or hang-up
	};
	// Waits up to timeoutMs for any entry to be ready. Returns false on a poll error.
	bool poll(std::vector<PollEntry>& entries, int timeoutMs);
}
