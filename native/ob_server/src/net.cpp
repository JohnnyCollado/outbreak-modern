// SPDX-License-Identifier: AGPL-3.0-or-later
#include "net.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <mutex>

namespace ob::server::net
{
#ifdef _WIN32
	const Socket kInvalid = static_cast<Socket>(INVALID_SOCKET);
#else
	const Socket kInvalid = -1;
#endif

	namespace
	{
		void setNonBlocking(Socket s)
		{
#ifdef _WIN32
			u_long on = 1;
			ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on);
#else
			fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
			int one = 1; // small request/answer packets: no Nagle delay
			setsockopt(static_cast<decltype(socket(0, 0, 0))>(s), IPPROTO_TCP, TCP_NODELAY,
				reinterpret_cast<const char*>(&one), sizeof(one));
		}

		bool wouldBlock()
		{
#ifdef _WIN32
			return WSAGetLastError() == WSAEWOULDBLOCK;
#else
			return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
		}
	} // namespace

	bool startup()
	{
#ifdef _WIN32
		static std::once_flag once;
		static bool ok = false;
		std::call_once(once, [] {
			WSADATA data;
			ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
		});
		return ok;
#else
		return true;
#endif
	}

	void closeSocket(Socket s)
	{
		if (s == kInvalid)
			return;
#ifdef _WIN32
		closesocket(static_cast<SOCKET>(s));
#else
		close(s);
#endif
	}

	Socket listenTcp(const std::string& address, uint16_t port, uint16_t* boundPort, std::string* error)
	{
		startup();
		const auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (static_cast<Socket>(s) == kInvalid)
		{
			*error = "socket";
			return kInvalid;
		}
		int one = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons(port);
		a.sin_addr.s_addr = htonl(INADDR_ANY);
		if (!address.empty() && inet_pton(AF_INET, address.c_str(), &a.sin_addr) != 1)
		{
			*error = "bad address " + address;
			closeSocket(static_cast<Socket>(s));
			return kInvalid;
		}
		if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 16) != 0)
		{
			*error = "bind/listen " + (address.empty() ? std::string("*") : address) + ":" + std::to_string(port);
			closeSocket(static_cast<Socket>(s));
			return kInvalid;
		}
		socklen_t length = sizeof(a);
		getsockname(s, reinterpret_cast<sockaddr*>(&a), &length);
		if (boundPort)
			*boundPort = ntohs(a.sin_port);
		setNonBlocking(static_cast<Socket>(s));
		return static_cast<Socket>(s);
	}

	Socket acceptTcp(Socket listener, std::string* peerAddress)
	{
		sockaddr_in a{};
		socklen_t length = sizeof(a);
		const auto s = accept(static_cast<decltype(socket(0, 0, 0))>(listener), reinterpret_cast<sockaddr*>(&a), &length);
		if (static_cast<Socket>(s) == kInvalid)
			return kInvalid;
		if (peerAddress)
		{
			char text[INET_ADDRSTRLEN] = {};
			inet_ntop(AF_INET, &a.sin_addr, text, sizeof(text));
			*peerAddress = text;
		}
		setNonBlocking(static_cast<Socket>(s));
		return static_cast<Socket>(s);
	}

	Socket connectTcp(const std::string& address, uint16_t port)
	{
		startup();
		const auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons(port);
		inet_pton(AF_INET, address.c_str(), &a.sin_addr);
		if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
		{
			closeSocket(static_cast<Socket>(s));
			return kInvalid;
		}
		setNonBlocking(static_cast<Socket>(s));
		return static_cast<Socket>(s);
	}

	long sendSome(Socket s, const uint8_t* data, size_t size)
	{
		const auto n = send(static_cast<decltype(socket(0, 0, 0))>(s), reinterpret_cast<const char*>(data),
			static_cast<int>(size), 0);
		if (n >= 0)
			return static_cast<long>(n);
		return wouldBlock() ? 0 : -1;
	}

	long receiveSome(Socket s, uint8_t* data, size_t size)
	{
		const auto n = recv(static_cast<decltype(socket(0, 0, 0))>(s), reinterpret_cast<char*>(data),
			static_cast<int>(size), 0);
		if (n > 0)
			return static_cast<long>(n);
		if (n == 0)
			return -1; // closed by the peer
		return wouldBlock() ? 0 : -1;
	}

	bool poll(std::vector<PollEntry>& entries, int timeoutMs)
	{
#ifdef _WIN32
		std::vector<WSAPOLLFD> fds(entries.size());
		for (size_t i = 0; i < entries.size(); i++)
		{
			fds[i].fd = static_cast<SOCKET>(entries[i].socket);
			fds[i].events = POLLRDNORM | (entries[i].wantWrite ? POLLWRNORM : 0);
		}
		const int rc = entries.empty() ? (Sleep(static_cast<DWORD>(timeoutMs)), 0)
									   : WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeoutMs);
#else
		std::vector<pollfd> fds(entries.size());
		for (size_t i = 0; i < entries.size(); i++)
		{
			fds[i].fd = entries[i].socket;
			fds[i].events = POLLIN | (entries[i].wantWrite ? POLLOUT : 0);
		}
		const int rc = ::poll(fds.data(), fds.size(), timeoutMs);
#endif
		if (rc < 0)
			return false;
		for (size_t i = 0; i < entries.size(); i++)
		{
			entries[i].readable = (fds[i].revents & (POLLIN | POLLHUP)) != 0;
			entries[i].writable = (fds[i].revents & POLLOUT) != 0;
			entries[i].failed = (fds[i].revents & (POLLERR | POLLNVAL)) != 0;
		}
		return true;
	}
}
