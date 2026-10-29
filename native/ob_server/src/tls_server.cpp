// SPDX-License-Identifier: AGPL-3.0-or-later
#include <algorithm>
#include <cctype>
#include "tls_server.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/dhm.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"

#include <cstring>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ob::server
{
	struct TlsWebServer::State
	{
		mbedtls_entropy_context entropy;
		mbedtls_ctr_drbg_context drbg;
		mbedtls_pk_context key;
		mbedtls_x509_crt cert;
		mbedtls_ssl_config conf;
		mbedtls_net_context listen;
		std::mutex lastMutex;
		ConnectionGuard guard;
		std::string last;

		State()
		{
			mbedtls_entropy_init(&entropy);
			mbedtls_ctr_drbg_init(&drbg);
			mbedtls_pk_init(&key);
			mbedtls_x509_crt_init(&cert);
			mbedtls_ssl_config_init(&conf);
			mbedtls_net_init(&listen);
		}
		~State()
		{
			mbedtls_net_free(&listen);
			mbedtls_ssl_config_free(&conf);
			mbedtls_x509_crt_free(&cert);
			mbedtls_pk_free(&key);
			mbedtls_ctr_drbg_free(&drbg);
			mbedtls_entropy_free(&entropy);
		}
	};

	namespace
	{
		// Server preference order: plain RSA + 3DES first (no DH-size question for the PS2's client), then DHE-RSA +
		// 3DES (the fan setup's suite), then AES for anything newer.
		const int kSuites[] = {
			MBEDTLS_TLS_RSA_WITH_3DES_EDE_CBC_SHA,
			MBEDTLS_TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,
			MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
			MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA,
			0,
		};

		std::string mbedError(const char* what, int code)
		{
			char text[160];
			mbedtls_strerror(code, text, sizeof(text));
			return std::string(what) + ": " + text;
		}

		// A self-signed certificate for the site name, SHA-1 signed (what 2003 clients verify), valid 2003-2049: both
		// dates stay UTCTime (DER writes 2050 on as GeneralizedTime, which the game's OpenSSL fails to decode with
		// an ASN.1 wrong-tag error; report entry 37).
		int makeCertificate(TlsWebServer::State* s, const std::string& host, std::string* error)
		{
			int rc = mbedtls_pk_setup(&s->key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
			if (rc == 0)
				rc = mbedtls_rsa_gen_key(mbedtls_pk_rsa(s->key), mbedtls_ctr_drbg_random, &s->drbg, 1024, 65537);
			if (rc != 0)
			{
				*error = mbedError("key", rc);
				return rc;
			}
			mbedtls_x509write_cert crt;
			mbedtls_x509write_crt_init(&crt);
			mbedtls_mpi serial;
			mbedtls_mpi_init(&serial);
			mbedtls_mpi_lset(&serial, 1);
			const std::string name = "CN=" + host + ",O=outbreak-modern";
			mbedtls_x509write_crt_set_subject_key(&crt, &s->key);
			mbedtls_x509write_crt_set_issuer_key(&crt, &s->key);
			mbedtls_x509write_crt_set_subject_name(&crt, name.c_str());
			mbedtls_x509write_crt_set_issuer_name(&crt, name.c_str());
			mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
			mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA1);
			mbedtls_x509write_crt_set_serial(&crt, &serial);
			mbedtls_x509write_crt_set_validity(&crt, "20030101000000", "20491231235959");
			std::vector<unsigned char> der(4096);
			rc = mbedtls_x509write_crt_der(&crt, der.data(), der.size(), mbedtls_ctr_drbg_random, &s->drbg);
			mbedtls_x509write_crt_free(&crt);
			mbedtls_mpi_free(&serial);
			if (rc < 0)
			{
				*error = mbedError("certificate", rc);
				return rc;
			}
			// The DER is written at the end of the buffer.
			rc = mbedtls_x509_crt_parse_der(&s->cert, der.data() + der.size() - rc, static_cast<size_t>(rc));
			if (rc != 0)
				*error = mbedError("certificate parse", rc);
			return rc;
		}
	} // namespace

	TlsWebServer::TlsWebServer() = default;

	TlsWebServer::~TlsWebServer()
	{
		stop();
	}

	bool TlsWebServer::start(const WebConfig& config, const std::string& address, uint16_t port, std::string* error)
	{
		std::string localError;
		if (!error)
			error = &localError;
		stop();
		m_state = std::make_unique<State>();
		m_config = config;
		State* s = m_state.get();
		const char* personal = "ob_server web";
		int rc = mbedtls_ctr_drbg_seed(&s->drbg, mbedtls_entropy_func, &s->entropy,
			reinterpret_cast<const unsigned char*>(personal), std::strlen(personal));
		if (rc != 0)
		{
			*error = mbedError("random", rc);
			return false;
		}
		if (makeCertificate(s, config.siteHost, error) != 0)
			return false;

		rc = mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT);
		if (rc != 0)
		{
			*error = mbedError("tls config", rc);
			return false;
		}
		mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->drbg);
		mbedtls_ssl_conf_min_version(&s->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_0);
		// A client that stops sending is dropped after 10 s (a stalled handshake or request holds this one thread).
		mbedtls_ssl_conf_read_timeout(&s->conf, 10000);
		s->guard.setLimits(config.limits);
		mbedtls_ssl_conf_max_version(&s->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
		mbedtls_ssl_conf_ciphersuites(&s->conf, kSuites);
		mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
		mbedtls_ssl_conf_read_timeout(&s->conf, 5000);
		static const unsigned char kDhP[] = MBEDTLS_DHM_RFC3526_MODP_2048_P_BIN;
		static const unsigned char kDhG[] = MBEDTLS_DHM_RFC3526_MODP_2048_G_BIN;
		mbedtls_ssl_conf_dh_param_bin(&s->conf, kDhP, sizeof(kDhP), kDhG, sizeof(kDhG));
		rc = mbedtls_ssl_conf_own_cert(&s->conf, &s->cert, &s->key);
		if (rc != 0)
		{
			*error = mbedError("own certificate", rc);
			return false;
		}

		const std::string portText = std::to_string(port);
		rc = mbedtls_net_bind(&s->listen, address.empty() ? nullptr : address.c_str(), portText.c_str(),
			MBEDTLS_NET_PROTO_TCP);
		if (rc != 0)
		{
			*error = mbedError(("bind " + address + ":" + portText).c_str(), rc);
			return false;
		}
		sockaddr_storage bound{};
		socklen_t length = sizeof(bound);
		if (getsockname(s->listen.fd, reinterpret_cast<sockaddr*>(&bound), &length) == 0)
			m_port = ntohs(bound.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port
														: reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
		m_running = true;
		m_thread = std::thread(&TlsWebServer::run, this);
		return true;
	}

	void TlsWebServer::stop()
	{
		if (!m_running.exchange(false))
		{
			m_state.reset();
			return;
		}
		mbedtls_net_free(&m_state->listen); // unblocks accept
		if (m_thread.joinable())
			m_thread.join();
		m_state.reset();
	}

	std::string TlsWebServer::lastSession() const
	{
		if (!m_state)
			return {};
		std::lock_guard<std::mutex> lock(m_state->lastMutex);
		return m_state->last;
	}

	void TlsWebServer::run()
	{
		State* s = m_state.get();
		while (m_running)
		{
			mbedtls_net_context client;
			mbedtls_net_init(&client);
			char ip[64] = {};
			size_t ipLength = 0;
			if (mbedtls_net_accept(&s->listen, &client, ip, sizeof(ip), &ipLength) != 0)
			{
				mbedtls_net_free(&client);
				continue; // stop() closed the socket, or a failed accept: m_running decides
			}
			std::string address;
			if (ipLength == 4)
				address = std::to_string(static_cast<unsigned char>(ip[0])) + "." +
						  std::to_string(static_cast<unsigned char>(ip[1])) + "." +
						  std::to_string(static_cast<unsigned char>(ip[2])) + "." +
						  std::to_string(static_cast<unsigned char>(ip[3]));
			// Local network only, and capped (guard.h): refused before the handshake.
			if (!s->guard.admit(address))
			{
				mbedtls_net_free(&client);
				if (m_config.log)
					m_config.log("[web] " + address + ": refused (not local, removed, or over the limit)");
				continue;
			}
			if (m_config.log)
				m_config.log("[web] " + address + ": accepted");

			mbedtls_ssl_context ssl;
			mbedtls_ssl_init(&ssl);
			bool ok = mbedtls_ssl_setup(&ssl, &s->conf) == 0;
			// The first bytes the client sent, shown when its handshake fails (what the game's SSL actually said).
			struct Recorded
			{
				mbedtls_net_context* net;
				std::string bytes;
			} recorded{&client, {}};
			mbedtls_ssl_set_bio(&ssl, &recorded, [](void* c, const unsigned char* b, size_t n) {
				return mbedtls_net_send(static_cast<Recorded*>(c)->net, b, n); }, nullptr,
				[](void* c, unsigned char* b, size_t n, uint32_t timeout) {
					Recorded* r = static_cast<Recorded*>(c);
					const int got = mbedtls_net_recv_timeout(r->net, b, n, timeout);
					if (got > 0 && r->bytes.size() < 512)
						r->bytes.append(reinterpret_cast<const char*>(b), std::min<size_t>(size_t(got), 512 - r->bytes.size()));
					return got;
				});
			int rc = 0;
			while (ok && (rc = mbedtls_ssl_handshake(&ssl)) != 0)
				if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
					ok = false;
			if (!ok)
			{
				m_failed++;
				if (m_config.log)
				{
					char hex[16];
					std::snprintf(hex, sizeof(hex), "-0x%04X", static_cast<unsigned>(-rc));
					std::string dump;
					for (unsigned char c : recorded.bytes)
					{
						char h[4];
						std::snprintf(h, sizeof(h), "%02x", c);
						dump += h;
					}
					m_config.log("[web] " + address + ": handshake failed " + hex + " after " + std::to_string(recorded.bytes.size()) +
								 " bytes: " + dump);
				}
			}
			else
			{
				if (m_config.log)
					m_config.log("[web] " + address + ": " + mbedtls_ssl_get_version(&ssl) + " " +
								 mbedtls_ssl_get_ciphersuite(&ssl));
				if (m_config.log && m_config.trace)
				{
					std::string dump;
					for (unsigned char c : recorded.bytes)
					{
						char h[4];
						std::snprintf(h, sizeof(h), "%02x", c);
						dump += h;
					}
					m_config.log("[web] " + address + ": handshake bytes " + dump);
				}
				{
					std::lock_guard<std::mutex> lock(s->lastMutex);
					s->last = std::string(mbedtls_ssl_get_version(&ssl)) + " " + mbedtls_ssl_get_ciphersuite(&ssl);
				}
				std::string request;
				unsigned char buffer[2048];
				while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384)
				{
					rc = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
					if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
						continue;
					if (rc <= 0)
						break;
					request.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(rc));
				}
				// A POST's body (the game's enterareas.html sends it in a second record): read all of it before the
				// reply. Closing with it unread resets the connection, and the client can lose the reply (entry 61).
				const size_t headerEnd = request.find("\r\n\r\n");
				if (headerEnd != std::string::npos)
				{
					std::string headers = request.substr(0, headerEnd);
					std::transform(headers.begin(), headers.end(), headers.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const size_t at = headers.find("\r\ncontent-length:");
					size_t length = 0;
					if (at != std::string::npos)
						for (size_t i = at + 17; i < headers.size() && headers[i] != '\r'; i++)
							if (headers[i] >= '0' && headers[i] <= '9')
								length = length * 10 + static_cast<size_t>(headers[i] - '0');
					const size_t want = headerEnd + 4 + std::min<size_t>(length, 65536);
					while (request.size() < want)
					{
						rc = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
						if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
							continue;
						if (rc <= 0)
							break;
						request.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(rc));
					}
				}
				const std::string path = webRequestPath(request);
				if (m_config.log)
				{
					std::string first = request.substr(0, request.find("\r\n"));
					if (first.size() > 160)
						first.resize(160);
					m_config.log("[web] " + address + ": request (" + std::to_string(request.size()) + " bytes, read " +
								 std::to_string(rc) + "): " + (first.empty() ? "<none>" : first));
				}
				if (!path.empty())
				{
					const std::string reply = webSerialize(webRespond(m_config, path, address));
					size_t sent = 0;
					while (sent < reply.size())
					{
						rc = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(reply.data()) + sent,
							reply.size() - sent);
						if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
							continue;
						if (rc <= 0)
							break;
						sent += static_cast<size_t>(rc);
					}
					m_served++;
				}
				mbedtls_ssl_close_notify(&ssl);
				// Whatever the client still sends (its close_notify) is read before the socket closes: a close with
				// unread data is a reset, which can throw away the reply before the client reads it. Up to ~2 s.
				for (int i = 0; i < 10; i++)
				{
					unsigned char drain[512];
					const int got = mbedtls_net_recv_timeout(&client, drain, sizeof(drain), 200);
					if (got == 0 || (got < 0 && got != MBEDTLS_ERR_SSL_TIMEOUT))
						break;
				}
			}
			mbedtls_ssl_free(&ssl);
			mbedtls_net_free(&client);
			s->guard.release(address);
		}
	}
}
