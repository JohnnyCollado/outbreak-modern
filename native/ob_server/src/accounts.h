// SPDX-License-Identifier: AGPL-3.0-or-later
// accounts: who is who for the built-in server (research/2026-10-27-lobby-protocol.md section 6). One account per
// player identity (step B: the device's address; step E: the device id from the app's hello), up to 3 handle names
// each (6 characters [0-9A-Z], unique across accounts, with a Shift-JIS nickname), and the 8-digit session numbers the
// login pages hand out and the lobby and relay look up. In memory; saving accounts and handles comes with step E.
// Thread-safe: the web and lobby threads share it.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace ob::server
{
	struct Handle
	{
		std::string id;       // 6 characters [0-9A-Z]
		std::string nickname; // Shift-JIS bytes as the game sent them
	};

	class Accounts
	{
	public:
		Accounts();
		// A new session for the account named by key (created on first use). 10000000..99999999, unique.
		uint32_t mintSession(const std::string& key);
		std::optional<std::string> accountForSession(uint32_t session) const;
		std::vector<Handle> handles(const std::string& account) const; // at most 3
		// "******" (the game's "new handle") makes a fresh unique id; otherwise the id must be one of the account's.
		// Sets the nickname. Returns the handle, or nullopt (unknown handle, or already 3).
		std::optional<Handle> selectHandle(const std::string& account, const std::string& id, const std::string& nickname);
		// The game number the account's players are in, 0 = none (the lobby sets it at the start, the relay reads it).
		void setGameNumber(const std::string& account, uint32_t game);
		uint32_t gameNumber(const std::string& account) const;

	private:
		mutable std::mutex m_mutex;
		std::mt19937 m_random;
		std::unordered_map<uint32_t, std::string> m_sessions;
		std::unordered_map<std::string, std::vector<Handle>> m_handles;
		std::unordered_map<std::string, uint32_t> m_games;
	};
}
