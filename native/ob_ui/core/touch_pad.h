// touch_pad: the on-screen touch controls' share of the PS2 pad (docs/plans/2026-10-26-touch-controls.md). The Kotlin
// overlay (native/android/kotlin TouchOverlay) sets what its fingers hold; the game thread's pad read
// (obui::filterPad) merges it into the host pad, as a second controller would: a button is down if either holds it,
// and a stick follows the touch stick while it is pushed, else the physical one. The same read counts the reads on
// which the physical pad was in use, so the overlay can hide itself while a controller plays (setting "Auto").
// Lock-free: one 64-bit atomic for the touch state, one counter; nothing else per read.
#pragma once

#include <atomic>
#include <cstdint>

namespace obui
{
	class TouchPad
	{
	public:
		// Any thread: what the touch controls hold. pressed: PS2 button bits, 1 = held (the same bit positions as the
		// game's active-low mask). sticks: right x, right y, left x, left y, 0x80 = centre (the runtime's order).
		void set(uint16_t pressed, const uint8_t sticks[4]);
		void clear();

		// Game thread: merges the touch state into the host pad the game is about to read (active-low buttons, sticks
		// in the order above) and counts physical use (a button down, or a stick past kPhysicalStick from centre).
		uint16_t merge(uint16_t buttonsActiveLow, uint8_t sticks[4]);

		// Reads on which the physical pad was in use, since start; wraps.
		uint32_t physicalReads() const { return m_physical.load(std::memory_order_relaxed); }

		// A touch stick within this of centre leaves the physical stick alone; a physical stick past kPhysicalStick
		// counts as use (a resting stick drifts a little; the runtime's dead zone already centred most of it).
		static constexpr int kTouchStick = 8;
		static constexpr int kPhysicalStick = 0x30;

	private:
		std::atomic<uint64_t> m_state{0x80808080ull << 16};
		std::atomic<uint32_t> m_physical{0};
	};

	// The process's one touch pad (the JNI side sets it, obui::filterPad merges it).
	TouchPad& touchPad();
} // namespace obui
