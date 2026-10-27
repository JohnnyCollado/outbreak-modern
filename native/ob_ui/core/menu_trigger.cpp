// menu_trigger: see menu_trigger.h.
#include "menu_trigger.h"

namespace obui
{
	namespace
	{
		constexpr int kLateTapReads = 4; // a tap that ended inside the hold-back window: shown for this many reads
	}

	MenuTrigger::MenuTrigger(Config config) : m_config(config) {}

	bool MenuTrigger::update(double now, bool backDown, bool startDown, bool guideDown, bool f1Pressed, bool escPressed)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		bool toggle = f1Pressed || escPressed;
		if (m_config.guideEnabled && guideDown && !m_guidePrev)
			toggle = true;
		m_guidePrev = guideDown;
		if (m_config.combo && backDown && startDown)
		{
			if (m_comboSince < 0.0)
				m_comboSince = now;
			if (!m_comboFired && now - m_comboSince >= m_config.comboHoldSec)
			{
				m_comboFired = true; // once per hold: let both go before the next one
				toggle = true;
			}
		}
		else
		{
			m_comboSince = -1.0;
			if (!backDown && !startDown)
				m_comboFired = false;
		}
		return toggle;
	}

	void MenuTrigger::setMenuOpen(bool open, double)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (m_open && !open)
			m_gateArmed = true;
		m_open = open;
	}

	float MenuTrigger::comboProgress(double now) const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (!m_config.combo || m_comboSince < 0.0 || m_comboFired || m_config.comboHoldSec <= 0.0)
			return 0.0f;
		const double p = (now - m_comboSince) / m_config.comboHoldSec;
		return static_cast<float>(p < 0.0 ? 0.0 : p > 1.0 ? 1.0 : p);
	}

	bool MenuTrigger::menuOpen() const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		return m_open;
	}

	uint16_t MenuTrigger::filterPad(uint16_t buttons, double now)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		const uint16_t pressed = static_cast<uint16_t>(~buttons);
		const bool selDown = (pressed & kPadSelect) != 0, startDown = (pressed & kPadStart) != 0;

		// Hold-back bookkeeping, also while the menu is open, so a combo that closes it is tracked to its release.
		auto track = [&](HeldBack& h, bool down) {
			if (down && !h.down)
			{
				h.since = now;
				h.released = false;
			}
			else if (!down && h.down && !h.released && !m_pairHidden && now - h.since < m_config.holdBackSec)
				h.lateTap = kLateTapReads; // let go inside the window: deliver it late
			h.down = down;
			if (down && !h.released && !m_pairHidden && now - h.since >= m_config.holdBackSec)
				h.released = true;
		};
		// holdBackSec 0 (the menu's choice since 2026-10-26, "no delay"): nothing is held back, not even a pair pressed on
		// the same read; update() still sees the combo.
		if (m_config.holdBackSec > 0.0 && selDown && startDown && !m_pairHidden)
		{
			// Both down: a combo when the second arrived inside the first one's window (neither passed through yet).
			if (!m_select.released && !m_start.released)
				m_pairHidden = true;
		}
		track(m_select, selDown);
		track(m_start, startDown);
		if (m_pairHidden && !selDown && !startDown)
		{
			m_pairHidden = false;
			m_select.lateTap = m_start.lateTap = 0;
		}

		if (m_open)
			return 0xFFFFu;

		uint16_t out = buttons;
		auto apply = [&](HeldBack& h, uint16_t bit) {
			if (h.lateTap > 0)
			{
				--h.lateTap;
				out = static_cast<uint16_t>(out & ~bit); // shown pressed
				return;
			}
			if (h.down && (m_pairHidden || !h.released))
				out = static_cast<uint16_t>(out | bit); // hidden
		};
		apply(m_select, kPadSelect);
		apply(m_start, kPadStart);

		if (m_gateArmed)
		{
			m_gateArmed = false;
			m_masked = pressed;
		}
		m_masked = static_cast<uint16_t>(m_masked & pressed); // a button let go is no longer masked
		out = static_cast<uint16_t>(out | m_masked);
		return out;
	}

	std::vector<std::string> reservedKeyNames()
	{
		return {"F1", "ESCAPE"};
	}
} // namespace obui
