// SPDX-License-Identifier: AGPL-3.0-or-later
#include "beacon.h"

#include "guard.h"

namespace ob::server
{
	namespace
	{
		constexpr const char* kMagic = "OBHOST1";

		// Decimal 1..max (or 0..max when zero is allowed), at most 5 digits, nothing else.
		bool parseNumber(const std::string& text, unsigned max, bool allowZero, unsigned& out)
		{
			if (text.empty() || text.size() > 5)
				return false;
			unsigned v = 0;
			for (char c : text)
			{
				if (c < '0' || c > '9')
					return false;
				v = v * 10 + static_cast<unsigned>(c - '0');
			}
			if (v > max || (v == 0 && !allowZero))
				return false;
			out = v;
			return true;
		}
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
		if (out.size() > kMaxHostName)
			out.resize(kMaxHostName);
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		return out;
	}

	std::string encodeBeacon(const BeaconInfo& info)
	{
		return std::string(kMagic) + "\nname=" + cleanHostName(info.name) + "\nweb=" + std::to_string(info.webPort) +
			   "\nlobby=" + std::to_string(info.lobbyPort) + "\nplayers=" + std::to_string(info.players) + "\n";
	}

	std::optional<BeaconInfo> decodeBeacon(const uint8_t* data, size_t size)
	{
		if (!data || size == 0 || size > kMaxBeaconBytes)
			return std::nullopt;
		const std::string text(reinterpret_cast<const char*>(data), size);
		if (text.back() != '\n')
			return std::nullopt;
		std::vector<std::string> lines;
		for (size_t at = 0; at < text.size();)
		{
			const size_t end = text.find('\n', at);
			lines.push_back(text.substr(at, end - at));
			at = end + 1;
		}
		if (lines.size() != 5 || lines[0] != kMagic)
			return std::nullopt;
		BeaconInfo info;
		bool name = false, web = false, lobby = false, players = false;
		for (size_t i = 1; i < lines.size(); i++)
		{
			const std::string& line = lines[i];
			const size_t eq = line.find('=');
			if (eq == std::string::npos)
				return std::nullopt;
			const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
			unsigned n = 0;
			if (key == "name" && !name)
			{
				if (value.empty() || value.size() > kMaxHostName)
					return std::nullopt;
				for (char c : value)
					if (c < 0x20 || c > 0x7E)
						return std::nullopt;
				info.name = value;
				name = true;
			}
			else if (key == "web" && !web && parseNumber(value, 65535, false, n))
			{
				info.webPort = static_cast<uint16_t>(n);
				web = true;
			}
			else if (key == "lobby" && !lobby && parseNumber(value, 65535, false, n))
			{
				info.lobbyPort = static_cast<uint16_t>(n);
				lobby = true;
			}
			else if (key == "players" && !players && parseNumber(value, 64, true, n))
			{
				info.players = static_cast<uint8_t>(n);
				players = true;
			}
			else
				return std::nullopt; // unknown, repeated or invalid field
		}
		return info;
	}

	bool BeaconSender::start(const BeaconInfo& info, const std::vector<std::string>& targets, uint16_t port,
		int intervalMs, std::string* error)
	{
		stop();
		m_socket = net::openUdp("", 0, true, nullptr, error);
		if (m_socket == net::kInvalid)
			return false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_info = info;
			m_info.name = cleanHostName(info.name);
		}
		m_running = true;
		m_thread = std::thread([this, targets, port, intervalMs] {
			while (m_running)
			{
				std::string wire;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					wire = encodeBeacon(m_info);
				}
				for (const std::string& t : targets)
					net::sendUdp(m_socket, t, port, reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
				for (int waited = 0; m_running && waited < intervalMs; waited += 20)
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		});
		return true;
	}

	void BeaconSender::setPlayers(uint8_t players)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_info.players = players;
	}

	void BeaconSender::stop()
	{
		m_running = false;
		if (m_thread.joinable())
			m_thread.join();
		if (m_socket != net::kInvalid)
		{
			net::closeSocket(m_socket);
			m_socket = net::kInvalid;
		}
	}

	bool BeaconListener::start(uint16_t port, std::string* error)
	{
		stop();
		m_socket = net::openUdp("", port, false, &m_port, error);
		if (m_socket == net::kInvalid)
			return false;
		m_running = true;
		m_thread = std::thread([this] {
			uint8_t buffer[kMaxBeaconBytes + 1];
			while (m_running)
			{
				std::string from;
				const long n = net::receiveUdp(m_socket, buffer, sizeof(buffer), &from);
				if (n <= 0)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					continue;
				}
				// The sender's address as delivered, local only; anything malformed or oversized is ignored.
				if (!isLocalAddress(from))
					continue;
				const auto info = decodeBeacon(buffer, static_cast<size_t>(n));
				if (!info)
					continue;
				std::lock_guard<std::mutex> lock(m_mutex);
				const auto now = std::chrono::steady_clock::now();
				// Hosts that went quiet make room (security queue S7: fakes could fill the 32 places for good).
				for (auto it = m_seen.begin(); it != m_seen.end();)
					if (now - it->second.at > std::chrono::milliseconds(m_staleMs))
						it = m_seen.erase(it);
					else
						++it;
				if (m_seen.size() >= kMaxListedHosts && !m_seen.count(from))
				{
					// Full of fresh entries: the oldest makes room, so re-sent fakes can't keep a real host out
					// (security queue 2, L2); a real host announces every second and comes straight back.
					auto oldest = m_seen.begin();
					for (auto it = m_seen.begin(); it != m_seen.end(); ++it)
						if (it->second.at < oldest->second.at)
							oldest = it;
					m_seen.erase(oldest);
				}
				m_seen[from] = Seen{*info, now};
			}
		});
		return true;
	}

	std::vector<FoundHost> BeaconListener::hosts(int maxAgeMs) const
	{
		const auto now = std::chrono::steady_clock::now();
		std::vector<FoundHost> out;
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& [address, seen] : m_seen)
			if (now - seen.at <= std::chrono::milliseconds(maxAgeMs))
				out.push_back(FoundHost{address, seen.info});
		return out;
	}

	void BeaconListener::stop()
	{
		m_running = false;
		if (m_thread.joinable())
			m_thread.join();
		if (m_socket != net::kInvalid)
		{
			net::closeSocket(m_socket);
			m_socket = net::kInvalid;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		m_seen.clear();
	}
}
