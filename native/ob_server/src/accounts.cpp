// SPDX-License-Identifier: AGPL-3.0-or-later
#include "accounts.h"

#include <chrono>

namespace ob::server
{
	Accounts::Accounts() : m_random(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {}

	uint32_t Accounts::mintSession(const std::string& key)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::uniform_int_distribution<uint32_t> pick(10000000u, 99999999u);
		uint32_t session;
		do
			session = pick(m_random);
		while (m_sessions.count(session));
		// One live session per account: a new login replaces the old (as the fan server does).
		for (auto it = m_sessions.begin(); it != m_sessions.end();)
			it = it->second == key ? m_sessions.erase(it) : std::next(it);
		m_sessions[session] = key;
		m_handles.try_emplace(key);
		return session;
	}

	std::optional<std::string> Accounts::accountForSession(uint32_t session) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_sessions.find(session);
		if (it == m_sessions.end())
			return std::nullopt;
		return it->second;
	}

	std::vector<Handle> Accounts::handles(const std::string& account) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_handles.find(account);
		return it == m_handles.end() ? std::vector<Handle>{} : it->second;
	}

	std::optional<Handle> Accounts::selectHandle(const std::string& account, const std::string& id, const std::string& nickname)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<Handle>& mine = m_handles[account];
		if (id == "******")
		{
			if (mine.size() >= 3)
				return std::nullopt;
			static const char kChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
			std::uniform_int_distribution<int> pick(0, 35);
			std::string fresh;
			for (bool taken = true; taken;)
			{
				fresh.clear();
				for (int i = 0; i < 6; i++)
					fresh.push_back(kChars[pick(m_random)]);
				taken = false;
				for (const auto& [owner, list] : m_handles)
					for (const Handle& h : list)
						taken = taken || h.id == fresh;
			}
			mine.push_back(Handle{fresh, nickname});
			return mine.back();
		}
		for (Handle& h : mine)
			if (h.id == id)
			{
				h.nickname = nickname;
				return h;
			}
		return std::nullopt;
	}

	void Accounts::setGameNumber(const std::string& account, uint32_t game)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_games[account] = game;
	}

	uint32_t Accounts::gameNumber(const std::string& account) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_games.find(account);
		return it == m_games.end() ? 0 : it->second;
	}
}
