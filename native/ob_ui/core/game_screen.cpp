// game_screen: see game_screen.h.
#include "game_screen.h"

namespace obui
{
	namespace
	{
		std::atomic<const uint8_t*> s_ram{nullptr};
		std::atomic<uint32_t> s_size{0};

		// The game writes these on its own thread; a torn or stale byte only picks the other layout for a tenth of a
		// second, so plain reads through volatile (no caching across polls) are enough.
		uint8_t U8(const uint8_t* ram, uint32_t addr) { return static_cast<const volatile uint8_t*>(ram)[addr]; }

		uint32_t U32(const uint8_t* ram, uint32_t addr)
		{
			return static_cast<uint32_t>(U8(ram, addr)) | static_cast<uint32_t>(U8(ram, addr + 1)) << 8 |
				static_cast<uint32_t>(U8(ram, addr + 2)) << 16 | static_cast<uint32_t>(U8(ram, addr + 3)) << 24;
		}
	} // namespace

	GameScreen classifyScreen(const uint8_t* ram, uint32_t size)
	{
		GameScreen g;
		if (!ram || size < kMenuOpen + 1 || size < kGameTaskSlot + 4)
			return g;
		g.known = true;
		g.controlType = U8(ram, kControlType);
		// Before the game task: logos, the title, scenario and character select. In it: the menu byte.
		g.menu = U32(ram, kGameTaskSlot) != kGameTaskFunc || U8(ram, kMenuOpen) != 0;
		return g;
	}

	void attachGuestRam(const uint8_t* ram, uint32_t size)
	{
		s_size.store(size);
		s_ram.store(ram);
	}

	GameScreen currentScreen() { return classifyScreen(s_ram.load(), s_size.load()); }
} // namespace obui
