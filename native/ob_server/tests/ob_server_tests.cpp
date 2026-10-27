// SPDX-License-Identifier: AGPL-3.0-or-later
// ob_server tests (ob.sh test): the wire format of research/2026-10-27-lobby-protocol.md section 3.
// - the 12-byte header round-trips, with FF FF FF in bytes 9..11 and the error flag in byte 8;
// - the lobby stream splits packets that arrive split or glued together;
// - the relay stream takes one header packet before login, then length-prefixed frames, and stops on length 0;
// - the scrambling decodes the version string in the fan server's packet capture (patch/update.txt: ciphertext
//   ae af a6 af b5 af a9 a0 a7 ac, packet id low byte 0x9B) into "0311172218", and round-trips;
// - the session number's 10 digits decode the capture's "2374125810" with packet id 0x4592 into 59318000, and
//   encode back.
#include "protocol.h"
#include "tls_server.h"
#include "web.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ob::server;

static int s_fail = 0, s_total = 0;
void TestLobby();       // lobby_tests.cpp
void TestServerChain(); // lobby_tests.cpp
void Check(bool ok, const char* what)
{
	++s_total;
	if (!ok)
	{
		++s_fail;
		std::printf("FAIL: %s\n", what);
	}
}

static Packet MakePacket(uint16_t cmd, uint16_t pid, std::vector<uint8_t> payload)
{
	Packet p;
	p.who = static_cast<uint8_t>(Who::Server);
	p.kind = static_cast<uint8_t>(Kind::Query);
	p.cmd = cmd;
	p.pid = pid;
	p.payload = std::move(payload);
	return p;
}

static void TestHeader()
{
	const std::vector<uint8_t> bytes = encode(MakePacket(0x6101, 1, {0x28, 0x37}));
	const std::vector<uint8_t> want = {0x18, 0x01, 0x61, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0x28, 0x37};
	Check(bytes == want, "LOGIN query encodes as 18 01 61 01 00 02 00 01 00 FF FF FF 28 37");

	Packet err = MakePacket(0x6406, 7, {});
	err.error = true;
	Check(encode(err)[8] == 0xFF, "an error answer sets header byte 8 to FF");
}

static void TestLobbyStream()
{
	const std::vector<uint8_t> a = encode(MakePacket(0x6203, 3, {}));
	const std::vector<uint8_t> b = encode(MakePacket(0x6204, 4, {0x00, 0x01}));
	std::vector<uint8_t> both = a;
	both.insert(both.end(), b.begin(), b.end());

	LobbyStream s;
	s.feed(both.data(), 5); // split inside the first header
	Check(!s.next().has_value(), "no packet from a partial header");
	s.feed(both.data() + 5, both.size() - 5);
	const auto p1 = s.next();
	const auto p2 = s.next();
	Check(p1 && p1->cmd == 0x6203 && p1->pid == 3 && p1->payload.empty(), "first of two glued packets");
	Check(p2 && p2->cmd == 0x6204 && p2->payload == std::vector<uint8_t>({0x00, 0x01}), "second of two glued packets");
	Check(!s.next().has_value() && s.buffered() == 0, "nothing left over");
}

static void TestRelayStream()
{
	Packet login = MakePacket(0x1031, 9, std::vector<uint8_t>(10, '0'));
	login.who = static_cast<uint8_t>(Who::RelayClient);
	login.kind = static_cast<uint8_t>(Kind::Tell);
	std::vector<uint8_t> bytes = encode(login);
	const std::vector<uint8_t> frameA = {3, 0xAA, 0xBB}, frameB = {2, 0xCC};
	bytes.insert(bytes.end(), frameA.begin(), frameA.end());
	bytes.insert(bytes.end(), frameB.begin(), frameB.end());

	RelayStream s;
	s.feed(bytes.data(), bytes.size());
	Check(!s.nextFrame().has_value(), "no game frames before login");
	const auto l = s.nextLogin();
	Check(l && l->who == 0x82 && l->cmd == 0x1031 && l->payload.size() == 10, "the GSLOGIN answer comes out first");
	s.setLoggedIn();
	Check(!s.nextLogin().has_value(), "no login packets after login");
	const auto f1 = s.nextFrame();
	const auto f2 = s.nextFrame();
	Check(f1 && *f1 == frameA && f2 && *f2 == frameB, "game frames come out whole, length byte included");

	const uint8_t partial[] = {4, 1};
	s.feed(partial, 2);
	Check(!s.nextFrame().has_value() && !s.broken(), "a partial frame waits");
	const uint8_t rest[] = {2, 3, 0, 9};
	s.feed(rest, sizeof(rest));
	const auto f3 = s.nextFrame();
	Check(f3 && f3->size() == 4, "the partial frame completes");
	Check(!s.nextFrame().has_value() && s.broken(), "a length-0 frame breaks the stream");
}

static void TestScramble()
{
	const uint8_t capture[] = {0xae, 0xaf, 0xa6, 0xaf, 0xb5, 0xaf, 0xa9, 0xa0, 0xa7, 0xac};
	std::vector<uint8_t> text(capture, capture + sizeof(capture));
	scramble(text.data(), text.size(), 0x9B);
	Check(std::string(text.begin(), text.end()) == "0311172218", "the captured version string decodes to 0311172218");
	scramble(text.data(), text.size(), 0x9B);
	Check(std::memcmp(text.data(), capture, sizeof(capture)) == 0, "scrambling twice gives the original back");

	// [length:2][sum:2][data], length counting the sum, as the game sends a string.
	std::vector<uint8_t> payload = {0x00, 0x00}; // a 2-byte field before the string
	std::string handle = "******";
	std::vector<uint8_t> data(handle.begin(), handle.end());
	scramble(data.data(), data.size(), 0x10);
	putU16(payload, static_cast<uint16_t>(data.size() + 2));
	putU16(payload, 0x1234);
	payload.insert(payload.end(), data.begin(), data.end());
	size_t next = 0;
	const auto s = readScrambledString(payload, 2, 0x10, &next);
	Check(s && *s == "******" && next == payload.size(), "a scrambled string reads back with its end offset");
	(void)readScrambledString(payload, 3, 0x10); // a misaligned read must not crash (any result is fine)
	std::vector<uint8_t> shortPayload = {0x00, 0x20, 0x00, 0x00, 'a'};
	Check(!readScrambledString(shortPayload, 0, 0).has_value(), "a string longer than the payload is refused");
}

static void TestSession()
{
	const char* capture = "2374125810";
	const auto session = decodeSessionDigits(reinterpret_cast<const uint8_t*>(capture), 0x4592);
	Check(session && *session == 59318000u, "the captured digits with pid 0x4592 decode to session 59318000");
	Check(encodeSessionDigits(59318000u, 0x4592) == "2374125810", "and encode back to the same digits");
	Check(!decodeSessionDigits(reinterpret_cast<const uint8_t*>("23741X5810"), 0x4592).has_value(),
		"non-digits are refused");
	Check(!decodeSessionDigits(reinterpret_cast<const uint8_t*>("0000100001"), 0x4592).has_value(),
		"halves below the packet id are refused");
	for (uint32_t s : {10000000u, 99999999u, 12345678u})
		for (uint16_t pid : {uint16_t(1), uint16_t(0xFFFF)})
		{
			const std::string d = encodeSessionDigits(s, pid);
			const auto back = decodeSessionDigits(reinterpret_cast<const uint8_t*>(d.data()), pid);
			Check(d.size() == 10 && back && *back == s, "session digits round-trip at the extremes");
		}
}

static WebConfig TestWebConfig()
{
	WebConfig c;
	c.mintSession = [](const std::string&) { return 12345678u; };
	return c;
}

// The login pages (research/2026-10-27-lobby-protocol.md section 2) with quick login.
static void TestWebPages()
{
	const WebConfig c = TestWebConfig();
	const WebResponse top = webRespond(c, "/00000002/CRS-top.jsp", "10.0.0.2");
	Check(top.status == 200 && top.body.find("<!--CRS-top-->") != std::string::npos &&
			  top.body.find("content=\"1; url=startsession.php?sessid=12345678\"") != std::string::npos,
		"CRS-top.jsp refreshes straight to the game-start page with a minted session (quick login)");

	const WebResponse start = webRespond(c, "/00000002/startsession.php?sessid=12345678", "10.0.0.2");
	Check(start.body.find("<!--CRS-game-start-->") != std::string::npos &&
			  start.body.find("CHARSET=EUC-JP") != std::string::npos &&
			  start.body.find("<!--<CSV>\"OK\",\"12345678\",\"https://www01.kddi-mmbb.jp/00000002/enterareas.html\",") !=
				  std::string::npos &&
			  start.body.find("\",</CSV>-->") != std::string::npos,
		"the game-start page carries the marker, EUC-JP and the CSV the game reads");

	const WebResponse lobby = webRespond(c, "/00000002/enterareas.html", "10.0.0.2");
	Check(lobby.body.find("<!--CRS-lbs-info-get-->") != std::string::npos &&
			  lobby.body.find("\"www01.kddi-mmbb.jp:8300\",") != std::string::npos,
		"the lobby list names our lobby by the game's host name and port 8300");

	Check(webRespond(c, "/other", "").status == 404, "anything else is 404");
	const std::string http = webSerialize(top);
	Check(http.rfind("HTTP/1.0 200 OK\r\n", 0) == 0 &&
			  http.find("Content-Length: " + std::to_string(top.body.size()) + "\r\n") != std::string::npos &&
			  http.find("Connection: close\r\n\r\n") != std::string::npos,
		"HTTP/1.0 with an exact Content-Length and no keep-alive");
	Check(webRequestPath("GET /00000002/CRS-top.jsp HTTP/1.0\r\nHost: x\r\n\r\n") == "/00000002/CRS-top.jsp" &&
			  webRequestPath("GET https://www01.kddi-mmbb.jp/00000002/a HTTP/1.0\r\n\r\n") == "/00000002/a" &&
			  webRequestPath("garbage").empty(),
		"request paths are read from the request line");
}

// One HTTPS GET over loopback with the protocol version and the single suite given; returns the body ("" on failure).
std::string FetchTls(uint16_t port, int minor, int suite, const char* path)
{
	mbedtls_net_context net;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_net_init(&net);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);
	std::string response;
	const int suites[] = {suite, 0};
	bool ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0) == 0 &&
			  mbedtls_net_connect(&net, "127.0.0.1", std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP) == 0 &&
			  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
				  MBEDTLS_SSL_PRESET_DEFAULT) == 0;
	if (ok)
	{
		mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
		mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
		mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, minor);
		mbedtls_ssl_conf_max_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, minor);
		mbedtls_ssl_conf_ciphersuites(&conf, suites);
		mbedtls_ssl_conf_read_timeout(&conf, 5000);
		ok = mbedtls_ssl_setup(&ssl, &conf) == 0;
		mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
	}
	int rc = 0;
	while (ok && (rc = mbedtls_ssl_handshake(&ssl)) != 0)
		if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
			ok = false;
	if (ok)
	{
		const std::string request = std::string("GET ") + path + " HTTP/1.0\r\nUser-Agent: test\r\n\r\n";
		ok = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(request.data()), request.size()) ==
			 static_cast<int>(request.size());
		unsigned char buffer[4096];
		while (ok && (rc = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer))) > 0)
			response.append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(rc));
	}
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
	mbedtls_net_free(&net);
	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);
	const size_t body = response.find("\r\n\r\n");
	return body == std::string::npos ? std::string() : response.substr(body + 4);
}

// The login pages over the TLS the PS2's browser may speak (protocol and suite each forced by the client).
static void TestTlsServer()
{
	TlsWebServer server;
	std::string error;
	const bool started = server.start(TestWebConfig(), "127.0.0.1", 0, &error);
	Check(started && server.port() != 0, "the TLS web server starts on a free loopback port");
	if (!started)
	{
		std::printf("  (%s)\n", error.c_str());
		return;
	}
	struct Case
	{
		int minor;
		int suite;
		const char* what;
	};
	const Case cases[] = {
		{MBEDTLS_SSL_MINOR_VERSION_1, MBEDTLS_TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA, "TLS 1.0 + DHE-RSA-3DES (the fan setup's suite)"},
		{MBEDTLS_SSL_MINOR_VERSION_1, MBEDTLS_TLS_RSA_WITH_3DES_EDE_CBC_SHA, "TLS 1.0 + RSA-3DES"},
		{MBEDTLS_SSL_MINOR_VERSION_0, MBEDTLS_TLS_RSA_WITH_3DES_EDE_CBC_SHA, "SSL 3.0 + RSA-3DES"},
		{MBEDTLS_SSL_MINOR_VERSION_0, MBEDTLS_TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA, "SSL 3.0 + DHE-RSA-3DES"},
	};
	for (const Case& c : cases)
	{
		const std::string body = FetchTls(server.port(), c.minor, c.suite, "/00000002/CRS-top.jsp");
		const bool ok = body.find("startsession.php?sessid=12345678") != std::string::npos;
		Check(ok, c.what);
		if (!ok)
			std::printf("  (handshake failures so far: %u)\n", server.handshakesFailed());
	}
	const std::string start = FetchTls(server.port(), MBEDTLS_SSL_MINOR_VERSION_1,
		MBEDTLS_TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA, "/00000002/startsession.php?sessid=12345678");
	Check(start.find("<!--CRS-game-start-->") != std::string::npos, "the game-start page over TLS");
	Check(server.lastSession().find("TLS-DHE-RSA-WITH-3DES-EDE-CBC-SHA") != std::string::npos,
		"the server reports the negotiated suite");
	server.stop();
}

int main()
{
	TestHeader();
	TestLobbyStream();
	TestRelayStream();
	TestScramble();
	TestSession();
	TestWebPages();
	TestTlsServer();
	TestLobby();
	TestServerChain();
	std::printf("ob_server: %d/%d\n", s_total - s_fail, s_total);
	return s_fail ? 1 : 0;
}
