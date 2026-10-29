// SPDX-License-Identifier: AGPL-3.0-or-later
// tls_server: serves the login pages (web.h) over the TLS the PS2's browser speaks: SSL 3.0 / TLS 1.0 up to TLS 1.2,
// RSA or DHE-RSA key exchange with 3DES first (the PS2's suites), on its own thread. A 1024-bit RSA key and a
// self-signed certificate for the site's name are made at start (self-signed is enough: Bioserver1's README).
#pragma once

#include "web.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace ob::server
{
	class TlsWebServer
	{
	public:
		TlsWebServer();
		~TlsWebServer();
		TlsWebServer(const TlsWebServer&) = delete;
		TlsWebServer& operator=(const TlsWebServer&) = delete;

		// Binds address:port (port 0 = any free port, see port()) and starts serving. false: *error says why.
		bool start(const WebConfig& config, const std::string& address, uint16_t port, std::string* error);
		void stop();
		uint16_t port() const { return m_port; }
		// The host removed this player: refused from now on (its page connections are short).
		void removeAddress(const std::string& address);
		uint32_t handshakesFailed() const { return m_failed.load(); }
		uint32_t requestsServed() const { return m_served.load(); }
		// The last negotiated protocol and suite, e.g. "TLSv1.0 TLS-DHE-RSA-WITH-3DES-EDE-CBC-SHA" (for logs/tests).
		std::string lastSession() const;

		struct State; // mbedTLS objects (tls_server.cpp)

	private:
		void run();
		std::unique_ptr<State> m_state;
		WebConfig m_config;
		std::thread m_thread;
		std::atomic<bool> m_running{false};
		std::atomic<uint32_t> m_failed{0}, m_served{0};
		uint16_t m_port = 0;
	};
}
