// touch_pad: see touch_pad.h.
#include "touch_pad.h"

#include <cstdlib>

namespace obui
{
	namespace
	{
		// Layout of the 64-bit state: bits 0-15 pressed buttons, 16-47 the four stick bytes (stick i at 16 + 8 i).
		uint64_t Pack(uint16_t pressed, const uint8_t sticks[4])
		{
			uint64_t v = pressed;
			for (int i = 0; i < 4; i++)
				v |= static_cast<uint64_t>(sticks[i]) << (16 + 8 * i);
			return v;
		}
	} // namespace

	void TouchPad::set(uint16_t pressed, const uint8_t sticks[4])
	{
		m_state.store(Pack(pressed, sticks), std::memory_order_relaxed);
	}

	void TouchPad::clear()
	{
		const uint8_t centre[4] = {0x80, 0x80, 0x80, 0x80};
		set(0, centre);
	}

	uint16_t TouchPad::merge(uint16_t buttons, uint8_t sticks[4])
	{
		bool physical = buttons != 0xFFFFu;
		for (int i = 0; i < 4 && !physical; i++)
			physical = std::abs(static_cast<int>(sticks[i]) - 0x80) > kPhysicalStick;
		if (physical)
			m_physical.fetch_add(1, std::memory_order_relaxed);

		const uint64_t v = m_state.load(std::memory_order_relaxed);
		const uint16_t pressed = static_cast<uint16_t>(v & 0xFFFFu);
		for (int i = 0; i < 4; i++)
		{
			const uint8_t t = static_cast<uint8_t>(v >> (16 + 8 * i));
			if (std::abs(static_cast<int>(t) - 0x80) > kTouchStick)
				sticks[i] = t;
		}
		return static_cast<uint16_t>(buttons & ~pressed);
	}

	TouchPad& touchPad()
	{
		static TouchPad s_pad;
		return s_pad;
	}
} // namespace obui
