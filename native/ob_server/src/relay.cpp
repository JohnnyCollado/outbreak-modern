// SPDX-License-Identifier: AGPL-3.0-or-later
#include "relay.h"

#include "net.h"
#include "protocol.h"

#include <chrono>
#include <list>
#include <vector>

namespace ob::server
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr uint16_t GSLOGIN = 0x1031;
	}

	struct RelayServer::Impl
	{
		struct Client
		{
			net::Socket socket = net::kInvalid;
			std::string address, account;
			RelayStream in;
			std::vector<uint8_t> out;
			uint32_t game = 0;
			bool loggedIn = false, closing = false;
			Clock::time_point heard = Clock::now();
		};

		Accounts* accounts = nullptr;
		RelayConfig config;
		RelayServer* owner = nullptr;
		net::Socket listener = net::kInvalid;
		std::list<Client> clients;
		uint16_t nextPid = 0;

		void log(const std::string& line) const
		{
			if (config.log)
				config.log("[relay] " + line);
		}

		void drop(Client& c, const std::string& why)
		{
			if (c.closing)
				return;
			c.closing = true;
			c.out.clear();
			log(c.address + ": " + why);
		}

		void login(Client& c, const Packet& p)
		{
			const auto session = p.cmd == GSLOGIN && p.payload.size() >= 10
				? decodeSessionDigits(p.payload.data(), p.pid)
				: std::nullopt;
			const auto account = session ? accounts->accountForSession(*session) : std::nullopt;
			const uint32_t game = account ? accounts->gameNumber(*account) : 0;
			if (!account || game == 0)
			{
				drop(c, account ? "not in a game, disconnected" : "unknown session, disconnected");
				return;
			}
			c.account = *account;
			c.game = game;
			c.loggedIn = true;
			c.in.setLoggedIn();
			owner->m_logins++;
			log(c.address + ": " + c.account + " in game " + std::to_string(game));
		}

		void relay(const Client& from, const std::vector<uint8_t>& frame)
		{
			owner->m_frames++;
			for (Client& o : clients)
				if (&o != &from && o.loggedIn && !o.closing && o.game == from.game)
					o.out.insert(o.out.end(), frame.begin(), frame.end());
		}
	};

	RelayServer::RelayServer() = default;
	RelayServer::~RelayServer() { stop(); }

	bool RelayServer::start(Accounts& accounts, const RelayConfig& config, const std::string& address, uint16_t port,
		std::string* error)
	{
		std::string localError;
		if (!error)
			error = &localError;
		stop();
		m_impl = std::make_unique<Impl>();
		m_impl->accounts = &accounts;
		m_impl->config = config;
		m_impl->owner = this;
		m_impl->listener = net::listenTcp(address, port, &m_port, error);
		if (m_impl->listener == net::kInvalid)
		{
			m_impl.reset();
			return false;
		}
		m_running = true;
		m_thread = std::thread([this] {
			Impl& s = *m_impl;
			std::vector<uint8_t> buffer(16384);
			while (m_running)
			{
				std::vector<net::PollEntry> entries;
				entries.push_back({s.listener});
				for (Impl::Client& c : s.clients)
					entries.push_back({c.socket, !c.out.empty()});
				net::poll(entries, 50);
				if (entries[0].readable)
				{
					std::string peer;
					for (net::Socket a; (a = net::acceptTcp(s.listener, &peer)) != net::kInvalid;)
					{
						Impl::Client& c = s.clients.emplace_back();
						c.socket = a;
						c.address = peer;
						Packet q; // GSLOGIN question: the game answers with its session digits
						q.who = static_cast<uint8_t>(Who::RelayServer);
						q.kind = static_cast<uint8_t>(Kind::Query);
						q.cmd = GSLOGIN;
						q.pid = ++s.nextPid;
						c.out = encode(q);
					}
				}
				size_t i = 1;
				for (Impl::Client& c : s.clients)
				{
					if (i >= entries.size())
						break;
					const net::PollEntry& e = entries[i++];
					if (e.failed)
						s.drop(c, "connection closed");
					if (c.closing || !e.readable)
						continue;
					const long n = net::receiveSome(c.socket, buffer.data(), buffer.size());
					if (n < 0)
					{
						s.drop(c, "connection closed");
						continue;
					}
					if (n == 0)
						continue;
					c.heard = Clock::now();
					c.in.feed(buffer.data(), static_cast<size_t>(n));
					if (!c.loggedIn)
						if (auto p = c.in.nextLogin())
							s.login(c, *p);
					while (c.loggedIn && !c.closing)
					{
						auto frame = c.in.nextFrame();
						if (!frame)
							break;
						s.relay(c, *frame);
					}
					if (c.in.broken())
						s.drop(c, "broken stream (length 0), disconnected");
				}
				const auto now = Clock::now();
				for (Impl::Client& c : s.clients)
					if (!c.closing && now - c.heard > std::chrono::seconds(s.config.idleSeconds))
						s.drop(c, "silent, disconnected");
				for (Impl::Client& c : s.clients)
					while (!c.out.empty())
					{
						const long n = net::sendSome(c.socket, c.out.data(), c.out.size());
						if (n < 0)
						{
							s.drop(c, "connection closed");
							break;
						}
						if (n == 0)
							break;
						c.out.erase(c.out.begin(), c.out.begin() + n);
					}
				for (auto it = s.clients.begin(); it != s.clients.end();)
					if (it->closing)
					{
						net::closeSocket(it->socket);
						it = s.clients.erase(it);
					}
					else
						++it;
			}
			for (Impl::Client& c : s.clients)
				net::closeSocket(c.socket);
			net::closeSocket(s.listener);
		});
		return true;
	}

	void RelayServer::stop()
	{
		if (m_running.exchange(false) && m_thread.joinable())
			m_thread.join();
		m_impl.reset();
	}
}
