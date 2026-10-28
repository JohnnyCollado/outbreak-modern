// ob_pcsx2_dev9 tests (ob.sh test): PCSX2's DEV9 + SMAP + Sockets adapter behind ob_dev9_bridge.h, without the IOP.
// Frames go out and come in through the SMAP registers as a PS2 Ethernet driver moves them (TX FIFO + TX buffer
// descriptor + EMAC3 TxMODE0 "get new packet"; RX buffer descriptor + RX FIFO), layouts from PCSX2 DEV9.h / smap.cpp.
// Nothing leaves the machine: DHCP and DNS are answered by the adapter's internal servers.
// - closed, DEV9 answers 0; open, the revision register reads 0x32 (the driver's "is a network adapter there");
// - a DHCP DISCOVER gets an OFFER with an address and a DNS server, and raises INTC line 13 (RXEND, unmasked);
// - a DNS query for gate1.jp.dnas.playstation.org (configured in mixed case) gets the configured host address;
// - the adapter's own messages reach the configured log file (the host-list answer is logged by name);
// - a remapped destination (alias 192.0.2.2:4433 -> a listener on 127.0.0.1) connects, the game seeing the alias;
// - PS2X_NET_REMAP's text form parses (address-only and address:port entries, malformed ones skipped);
// - DMA 8 takes only psxDma8's CHCRs.
// With no usable network interface the Sockets adapter can't start: the network checks are skipped, said so.
#include "ob_dev9_bridge.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static int s_fail = 0, s_total = 0;
static void Check(bool ok, const char* what)
{
	++s_total;
	if (!ok)
	{
		++s_fail;
		std::printf("FAIL: %s\n", what);
	}
}

// DEV9.h
constexpr uint32_t kRev = 0x1F80146Eu;
constexpr uint32_t kSpd = 0x10000000u, kIntrMask = kSpd + 0x2A;
constexpr uint32_t kSmap = kSpd + 0x100;
constexpr uint32_t kTxWrPtr = kSmap + 0xF04, kTxData = kSmap + 0x1000;
constexpr uint32_t kRxRdPtr = kSmap + 0xF34, kRxData = kSmap + 0x1100;
constexpr uint32_t kTxMode0L = kSmap + 0x1F00 + 0x08; // SMAP_EMAC3_REGBASE + 0x08
constexpr uint32_t kBdTx = kSmap + 0x2F00, kBdRx = kBdTx + 0x200;
constexpr uint16_t kBdTxReady = 0x8000, kBdRxEmpty = 0x8000;
constexpr uint16_t kIntrRxEnd = 1 << 5, kIntrTxEnd = 1 << 4;
const uint8_t kPs2Mac[6] = {0x00, 0x04, 0x1F, 0x82, 0x30, 0x31};

static uint32_t s_txBd = 0, s_rxBd = 0;

static uint16_t Sum16(const uint8_t* p, size_t n, uint32_t s = 0)
{
	for (size_t i = 0; i + 1 < n; i += 2)
		s += (p[i] << 8) | p[i + 1];
	if (n & 1)
		s += p[n - 1] << 8;
	while (s >> 16)
		s = (s & 0xFFFF) + (s >> 16);
	return static_cast<uint16_t>(~s);
}

static void Put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = x >> 8; v[at + 1] = x & 0xFF; }
static uint16_t Get16(const std::vector<uint8_t>& v, size_t at) { return (v[at] << 8) | v[at + 1]; }

// Ethernet + IPv4 + UDP around `payload`.
static std::vector<uint8_t> UdpFrame(const uint8_t src[4], const uint8_t dst[4], uint16_t sport, uint16_t dport,
	const std::vector<uint8_t>& payload)
{
	std::vector<uint8_t> f(14 + 20 + 8 + payload.size(), 0);
	std::memset(&f[0], 0xFF, 6);
	std::memcpy(&f[6], kPs2Mac, 6);
	Put16(f, 12, 0x0800);
	uint8_t* ip = &f[14];
	ip[0] = 0x45;
	Put16(f, 16, static_cast<uint16_t>(20 + 8 + payload.size()));
	ip[8] = 64;
	ip[9] = 17;
	std::memcpy(ip + 12, src, 4);
	std::memcpy(ip + 16, dst, 4);
	Put16(f, 24, Sum16(ip, 20));
	Put16(f, 34, sport);
	Put16(f, 36, dport);
	Put16(f, 38, static_cast<uint16_t>(8 + payload.size()));
	std::memcpy(&f[42], payload.data(), payload.size()); // UDP checksum 0 = none
	return f;
}

// The frame into the TX FIFO at 0 and out through the next TX descriptor, as smap.cpp tx_process reads it.
static void Send(const std::vector<uint8_t>& frame)
{
	ob_dev9_write32(kTxWrPtr, 0);
	for (size_t i = 0; i < frame.size(); i += 4)
	{
		uint32_t w = 0;
		std::memcpy(&w, &frame[i], std::min<size_t>(4, frame.size() - i));
		ob_dev9_write32(kTxData, w);
	}
	const uint32_t bd = kBdTx + s_txBd * 8;
	ob_dev9_write16(bd + 2, 0);
	ob_dev9_write16(bd + 4, static_cast<uint16_t>(frame.size()));
	ob_dev9_write16(bd + 6, 0x1000);
	ob_dev9_write16(bd + 0, kBdTxReady);
	s_txBd = (s_txBd + 1) & 63;
	// EMAC3 registers take their halves swapped (smap.cpp wswap): "get new packet" (bit 31) is bit 15 of the low half.
	ob_dev9_write32(kTxMode0L, 0x00008000u);
}

// Waits (advancing IOP time) for the next RX descriptor to fill; returns its frame, empty on timeout.
static std::vector<uint8_t> Receive(bool* irq, int tries = 3000)
{
	const uint32_t bd = kBdRx + s_rxBd * 8;
	for (int i = 0; i < tries; i++)
	{
		ob_dev9_advance(1000);
		if (ob_dev9_take_interrupt())
			*irq = true;
		if (!(ob_dev9_read16(bd) & kBdRxEmpty))
		{
			const uint16_t len = ob_dev9_read16(bd + 4), ptr = ob_dev9_read16(bd + 6);
			ob_dev9_write32(kRxRdPtr, (ptr - 0x4000u) & 16383u);
			std::vector<uint8_t> f((len + 3) & ~3u);
			for (size_t j = 0; j < f.size(); j += 4)
			{
				const uint32_t w = ob_dev9_read32(kRxData);
				std::memcpy(&f[j], &w, 4);
			}
			f.resize(len);
			ob_dev9_write16(bd, kBdRxEmpty); // hand it back to the hardware
			s_rxBd = (s_rxBd + 1) & 63;
			return f;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return {};
}

static bool IsUdpFrom(const std::vector<uint8_t>& f, uint16_t sport)
{
	return f.size() >= 42 && Get16(f, 12) == 0x0800 && f[23] == 17 && Get16(f, 34) == sport;
}

// Ethernet + IPv4 + TCP SYN (no options), with the IP and TCP checksums (RFC 793 pseudo header).
static std::vector<uint8_t> TcpSynFrame(const uint8_t src[4], const uint8_t dst[4], uint16_t sport, uint16_t dport)
{
	std::vector<uint8_t> f(14 + 20 + 20, 0);
	std::memset(&f[0], 0xFF, 6);
	std::memcpy(&f[6], kPs2Mac, 6);
	Put16(f, 12, 0x0800);
	uint8_t* ip = &f[14];
	ip[0] = 0x45;
	Put16(f, 16, 40);
	ip[8] = 64;
	ip[9] = 6;
	std::memcpy(ip + 12, src, 4);
	std::memcpy(ip + 16, dst, 4);
	Put16(f, 24, Sum16(ip, 20));
	Put16(f, 34, sport);
	Put16(f, 36, dport);
	f[38] = 0; f[39] = 0; f[40] = 0x03; f[41] = 0xE8; // sequence 1000
	f[46] = 0x50;                                  // header 20 bytes
	f[47] = 0x02;                                  // SYN
	Put16(f, 48, 8192);                            // window
	std::vector<uint8_t> pseudo(12 + 20);
	std::memcpy(&pseudo[0], src, 4);
	std::memcpy(&pseudo[4], dst, 4);
	pseudo[9] = 6;
	pseudo[11] = 20;
	std::memcpy(&pseudo[12], &f[34], 20);
	Put16(f, 50, Sum16(pseudo.data(), pseudo.size()));
	return f;
}

static bool IsTcpSynAckFrom(const std::vector<uint8_t>& f, const uint8_t ip[4], uint16_t sport)
{
	return f.size() >= 54 && Get16(f, 12) == 0x0800 && f[23] == 6 && std::memcmp(&f[26], ip, 4) == 0 &&
		   Get16(f, 34) == sport && (f[47] & 0x12) == 0x12;
}

// A TCP listener on 127.0.0.1 at a free port (the stand-in for this device's own server).
struct LoopbackListener
{
	SOCKET s = INVALID_SOCKET;
	uint16_t port = 0;
	LoopbackListener()
	{
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		int len = sizeof(a);
		if (s == INVALID_SOCKET || bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 1) != 0 ||
			getsockname(s, reinterpret_cast<sockaddr*>(&a), &len) != 0)
			return;
		port = ntohs(a.sin_port);
	}
	~LoopbackListener()
	{
		if (s != INVALID_SOCKET)
			closesocket(s);
	}
	bool Pending() const // a connection waits to be accepted
	{
		fd_set r;
		FD_ZERO(&r);
		FD_SET(s, &r);
		timeval none{};
		return select(static_cast<int>(s + 1), &r, nullptr, nullptr, &none) == 1;
	}
	uint32_t AcceptPeer() const // accepts the waiting connection; its source address (host order), 0 if none
	{
		sockaddr_in a{};
		int len = sizeof(a);
		const SOCKET c = accept(s, reinterpret_cast<sockaddr*>(&a), &len);
		if (c == INVALID_SOCKET)
			return 0;
		closesocket(c);
		return ntohl(a.sin_addr.s_addr);
	}
};

// PS2X_NET_REMAP's text form (no network needed).
static void TestParseRemaps()
{
	ob_dev9_remap r[4] = {};
	uint32_t bad = 0;
	const uint32_t n = ob_dev9_parse_remaps("192.0.2.2=127.0.0.1; 192.0.2.2:443=127.0.0.1:18443;nonsense;1.2.3=4.5.6.7",
		r, 4, &bad);
	Check(n == 2 && bad == 2, "two remaps read, two malformed entries skipped");
	Check(r[0].from[0] == 192 && r[0].from[3] == 2 && r[0].from_port == 0 && r[0].to[0] == 127 && r[0].to[3] == 1 &&
			  r[0].to_port == 0,
		"an address-only remap keeps any port");
	Check(r[1].from_port == 443 && r[1].to_port == 18443, "a remap with ports moves the port");
	Check(r[0].source[0] == 0, "no @source = the host picks the source address");
	Check(ob_dev9_parse_remaps("192.0.2.2=127.0.0.1@127.0.0.2", r, 4, &bad) == 1 && bad == 0 && r[0].source[0] == 127 &&
			  r[0].source[3] == 2 && r[0].to[3] == 1,
		"@source gives the host socket's own address (a second client on the same PC)");
	Check(ob_dev9_parse_remaps("192.0.2.2=127.0.0.1@nope", r, 4, &bad) == 0 && bad == 1, "a malformed @source is skipped");
	Check(ob_dev9_parse_remaps("", r, 4, &bad) == 0 && ob_dev9_parse_remaps(nullptr, r, 4, &bad) == 0,
		"empty or missing text = no remaps");
}

int main()
{
	TestParseRemaps();
	Check(ob_dev9_read8(kRev) == 0, "closed: DEV9 answers 0 (the driver's 'unknown dev9 hardware')");

	const ob_dev9_host hosts[] = {{"GATE1.jp.dnas.playstation.org", {10, 1, 2, 3}}};
	const char* kLog = "ob_dev9_tests.log";
	std::remove(kLog);
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	LoopbackListener server;
	const uint8_t kAlias[4] = {192, 0, 2, 2};
	const ob_dev9_remap remaps[] = {{{192, 0, 2, 2}, 4433, {127, 0, 0, 1}, server.port, {}},
		{{192, 0, 2, 2}, 4434, {127, 0, 0, 1}, server.port, {127, 0, 0, 2}}};
	const ob_dev9_config config{nullptr, hosts, 1, kLog, remaps, 2};
	if (!ob_dev9_open(&config))
	{
		std::printf("SKIP: no usable network interface for the Sockets adapter; network checks not run\n");
		ob_dev9_close();
		std::printf("pcsx2_dev9: %d/%d\n", s_total - s_fail, s_total);
		return s_fail ? 1 : 0;
	}
	Check(ob_dev9_network_up(), "open: the Sockets adapter started");
	Check(ob_dev9_read8(kRev) == 0x32 && ob_dev9_read16(kRev) == 0x32, "open: the revision register reads 0x32");

	for (uint32_t i = 0; i < 64; i++) // every RX descriptor empty (owned by the hardware), as the driver sets them
		ob_dev9_write16(kBdRx + i * 8, kBdRxEmpty);
	ob_dev9_write16(kIntrMask, kIntrRxEnd | kIntrTxEnd);

	// DHCP DISCOVER (RFC 2131): op 1, Ethernet, xid, client MAC, magic cookie, option 53 = 1, and option 55 asking
	// for the subnet mask, router and DNS server (the server sends only what is asked for).
	std::vector<uint8_t> dhcp(240, 0);
	dhcp[0] = 1; dhcp[1] = 1; dhcp[2] = 6;
	dhcp[4] = 0x0B; dhcp[5] = 0xAD; dhcp[6] = 0xF0; dhcp[7] = 0x0D;
	std::memcpy(&dhcp[28], kPs2Mac, 6);
	dhcp[236] = 99; dhcp[237] = 130; dhcp[238] = 83; dhcp[239] = 99;
	dhcp.insert(dhcp.end(), {53, 1, 1, 55, 3, 1, 3, 6, 255});
	const uint8_t any[4] = {0, 0, 0, 0}, all[4] = {255, 255, 255, 255};
	Send(UdpFrame(any, all, 68, 67, dhcp));
	Check(!(ob_dev9_read16(kBdTx) & kBdTxReady), "the SMAP took the frame (TX descriptor handed back)");
	bool irq = false;
	std::vector<uint8_t> offer = Receive(&irq);
	Check(IsUdpFrom(offer, 67), "a DHCP reply comes back through the RX FIFO");
	uint8_t yiaddr[4] = {}, dns[4] = {};
	bool isOffer = false;
	if (IsUdpFrom(offer, 67) && offer.size() >= 42 + 240)
	{
		const uint8_t* d = &offer[42];
		Check(d[0] == 2 && d[4] == 0x0B && d[7] == 0x0D, "the reply answers our DISCOVER (op 2, same xid)");
		std::memcpy(yiaddr, d + 16, 4);
		for (size_t o = 240; 42 + o + 1 < offer.size() && d[o] != 255;)
		{
			if (d[o] == 0) { o++; continue; }
			const uint8_t code = d[o], len = d[o + 1];
			if (code == 53 && len == 1)
				isOffer = d[o + 2] == 2;
			if (code == 6 && len >= 4)
				std::memcpy(dns, d + o + 2, 4);
			o += 2 + len;
		}
	}
	Check(isOffer, "it is a DHCP OFFER (option 53 = 2)");
	Check(yiaddr[0] != 0, "the OFFER gives the PS2 an address");
	Check(dns[0] != 0, "the OFFER names a DNS server");
	Check(irq, "the received frame raised INTC line 13 (RXEND unmasked)");

	// DNS query (RFC 1035) for the DNAS host in lower case, as the game's strings have it (the entry above is mixed
	// case: ob_dev9_open lowers it). PCSX2's DNS_Server answers an upper-case question with SERVFAIL even when listed:
	// CheckHostList stores the answer under the lowered name, FinaliseDNS looks it up under the asked one.
	std::vector<uint8_t> q = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
	for (const char* label : {"gate1", "jp", "dnas", "playstation", "org"})
	{
		q.push_back(static_cast<uint8_t>(std::strlen(label)));
		q.insert(q.end(), label, label + std::strlen(label));
	}
	q.insert(q.end(), {0, 0, 1, 0, 1});
	Send(UdpFrame(yiaddr, dns, 40000, 53, q));
	irq = false;
	std::vector<uint8_t> answer;
	for (int tries = 0; tries < 4 && !IsUdpFrom(answer, 53); tries++)
		answer = Receive(&irq);
	Check(IsUdpFrom(answer, 53), "a DNS reply comes back");
	bool found = false;
	if (IsUdpFrom(answer, 53) && answer.size() >= 42 + 12)
	{
		for (size_t i = 42 + 12; i + 4 <= answer.size(); i++) // the A record's address, anywhere after the header
			if (answer[i] == 10 && answer[i + 1] == 1 && answer[i + 2] == 2 && answer[i + 3] == 3)
				found = true;
		Check((answer[42 + 2] & 0x80) && Get16(answer, 42 + 6) >= 1, "the reply is a response with an answer");
	}
	Check(found, "gate1.jp.dnas.playstation.org resolves to the configured 10.1.2.3");
	if (!found)
	{
		std::printf("  DNS reply (%zu bytes) from %u.%u.%u.%u:", answer.size(), dns[0], dns[1], dns[2], dns[3]);
		for (size_t i = 42; i < answer.size() && i < 42 + 96; i++)
			std::printf(" %02X", answer[i]);
		std::printf("\n");
	}
	// DNS_Server.cpp logs a host-list answer ("DEV9: DNS: <name> found in hosts"); the adapter's log file has it.
	std::string log;
	if (FILE* f = std::fopen(kLog, "rb"))
	{
		char buf[4096];
		for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;)
			log.append(buf, n);
		std::fclose(f);
	}
	Check(log.find("gate1.jp.dnas.playstation.org found in hosts") != std::string::npos,
		"the adapter's log names the host-list answer");

	// A remapped destination: the game connects to the alias 192.0.2.2:4433; the host socket reaches the listener on
	// 127.0.0.1, and the game's SYN-ACK still comes from 192.0.2.2:4433.
	Check(server.port != 0, "a loopback listener for the remap test");
	Send(TcpSynFrame(yiaddr, kAlias, 40001, 4433));
	bool accepted = false, synAck = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10); // bounded: no answer = fail
	while (!(accepted && synAck) && std::chrono::steady_clock::now() < deadline)
	{
		const std::vector<uint8_t> f = Receive(&irq, 50);
		synAck |= IsTcpSynAckFrom(f, kAlias, 4433);
		accepted |= server.Pending();
	}
	Check(accepted, "a connection to the remapped alias reaches the listener on 127.0.0.1");
	Check(synAck, "the game's SYN-ACK comes from the alias address and port");
	Check(accepted && server.AcceptPeer() == 0x7F000001u, "without @source the server sees 127.0.0.1");

	// The same with @source 127.0.0.2: the server sees the connection from 127.0.0.2 (two clients on one PC are told
	// apart by address).
	Send(TcpSynFrame(yiaddr, kAlias, 40002, 4434));
	accepted = false;
	const auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!accepted && std::chrono::steady_clock::now() < deadline2)
	{
		Receive(&irq, 50);
		accepted |= server.Pending();
	}
	Check(accepted && server.AcceptPeer() == 0x7F000002u, "with @source 127.0.0.2 the server sees 127.0.0.2");

	std::vector<uint8_t> ram(0x10000, 0);
	Check(ob_dev9_dma(ram.data(), static_cast<uint32_t>(ram.size()), 0x100, (1u << 16) | 1u, 0x00000201u) ==
			  OB_DEV9_DMA_IGNORED,
		"DMA 8 ignores a CHCR psxDma8 does not take");

	ob_dev9_close();
	Check(ob_dev9_read8(kRev) == 0, "closed again: DEV9 answers 0");
	std::printf("pcsx2_dev9: %d/%d\n", s_total - s_fail, s_total);
	return s_fail ? 1 : 0;
}
