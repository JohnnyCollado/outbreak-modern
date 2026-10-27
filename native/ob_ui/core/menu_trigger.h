// menu_trigger: when the in-game menu opens and closes, and what the game sees of the pad around it
// (docs/plans/2026-10-26-settings-menu.md decisions P1-P5, sections 5.2-5.3).
// - Keyboard: F1 or Esc toggles at once (both reserved: the binding screen refuses them).
// - Controller: Back + Start held together for comboHoldSec toggles when Config::combo (on by default; the menu turns it
//   off since 2026-10-26: Start/Select are plain game buttons); Guide too when enabled (off by default).
// - Hold-back: the game sees a Back (PS2 Select) or Start press only after holdBackSec, or not at all when it turns
//   into the combo; a tap shorter than that reaches the game late, as a short press.
// - Pad gate: while the menu is open the game reads every button released; after it closes, each button stays
//   released for the game until it has been let go once (no stuck or accidental presses).
// update() runs on the main thread once per host frame, filterPad() on the game thread; one mutex between them.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace obui
{
	// PS2 pad buttons, active low (0 = pressed), as the game reads them (libpad's 16-bit mask).
	enum PadBit : uint16_t
	{
		kPadSelect = 1u << 0,
		kPadL3 = 1u << 1,
		kPadR3 = 1u << 2,
		kPadStart = 1u << 3,
	};

	class MenuTrigger
	{
	public:
		struct Config
		{
			double comboHoldSec = 1.0;
			double holdBackSec = 0.1;
			bool guideEnabled = false;
			bool combo = true; // false: Back + Start never toggles (the menu's setting since 2026-10-26)
		};
		MenuTrigger() : MenuTrigger(Config{}) {} // not a default argument: clang wants Config complete first
		explicit MenuTrigger(Config config);

		// Main thread: the controller's physical Back / Start / Guide and this frame's F1 / Esc presses. True on the
		// frame the menu should open or close; the caller then calls setMenuOpen.
		bool update(double now, bool backDown, bool startDown, bool guideDown, bool f1Pressed, bool escPressed);
		void setMenuOpen(bool open, double now);
		bool menuOpen() const;
		// How far Back + Start are into the combo hold, 0..1 (0 = not both held, combo off, or already fired this
		// hold); as of the last update(). For the hold ring.
		float comboProgress(double now) const;

		// Game thread: the buttons the game is about to read (active low) -> what it gets.
		uint16_t filterPad(uint16_t buttonsActiveLow, double now);

	private:
		struct HeldBack
		{
			bool down = false;     // as last seen by filterPad
			double since = 0.0;    // when it went down
			bool released = false; // passed through to the game (the window ran out alone)
			int lateTap = 0;       // reads left to show a tap that ended inside the window
		};

		Config m_config;
		mutable std::mutex m_mutex;
		bool m_open = false;
		// update(): the combo and Guide edge.
		double m_comboSince = -1.0;
		bool m_comboFired = false;
		bool m_guidePrev = false;
		// filterPad(): hold-back of Select and Start, and the gate.
		HeldBack m_select, m_start;
		bool m_pairHidden = false; // both went down inside one window: hidden until both are up
		bool m_gateArmed = false;  // the menu just closed: the next read takes the held buttons as masked
		uint16_t m_masked = 0;     // pressed bits (1 = masked) hidden until released once
	};

	// Key names (input.ini spelling) the binding screen refuses: they open the menu.
	std::vector<std::string> reservedKeyNames();
} // namespace obui
