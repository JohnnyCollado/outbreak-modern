// SPDX-License-Identifier: AGPL-3.0-or-later
#include "guard.h"

#include <array>

namespace ob::server
{
	bool parseIpv4(const std::string& text, std::array<uint8_t, 4>& out)
	{
		size_t part = 0, digits = 0;
		unsigned value = 0;
		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				if (++digits > 3)
					return false;
				value = value * 10 + static_cast<unsigned>(c - '0');
				if (value > 255)
					return false;
			}
			else if (c == '.')
			{
				if (digits == 0 || part == 3)
					return false;
				out[part++] = static_cast<uint8_t>(value);
				value = 0;
				digits = 0;
			}
			else
				return false;
		}
		if (digits == 0 || part != 3)
			return false;
		out[3] = static_cast<uint8_t>(value);
		return true;
	}

	bool isLocalAddress(const std::string& address)
	{
		std::array<uint8_t, 4> a{};
		if (!parseIpv4(address, a))
			return false;
		return a[0] == 10 || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 192 && a[1] == 168) ||
			   (a[0] == 169 && a[1] == 254) || a[0] == 127;
	}

	void ConnectionGuard::setLimits(GuardLimits limits)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_limits = limits;
	}

	bool ConnectionGuard::admit(const std::string& address)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!isLocalAddress(address) || m_removed.count(address) || m_total >= m_limits.total)
			return false;
		size_t& open = m_open[address];
		if (open >= m_limits.perAddress)
			return false;
		++open;
		++m_total;
		return true;
	}

	void ConnectionGuard::release(const std::string& address)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_open.find(address);
		if (it == m_open.end() || it->second == 0)
			return;
		if (--it->second == 0)
			m_open.erase(it);
		--m_total;
	}

	void ConnectionGuard::remove(const std::string& address)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_removed.insert(address);
	}

	bool ConnectionGuard::removed(const std::string& address) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_removed.count(address) != 0;
	}
}
