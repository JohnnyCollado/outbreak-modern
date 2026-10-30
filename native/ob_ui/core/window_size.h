// window_size: the "window" setting ("1920x1080"), applied to the game window when it starts (the launcher keeps its
// own window). False for anything but two sizes from 1 to 16384.
#pragma once

#include <string>

namespace obui
{
	inline bool parseWindowSize(const std::string& text, int& width, int& height)
	{
		const size_t x = text.find('x');
		if (x == std::string::npos || x == 0 || x + 1 == text.size() || x > 5 || text.size() - x - 1 > 5)
			return false;
		int w = 0, h = 0;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (i == x)
				continue;
			if (text[i] < '0' || text[i] > '9')
				return false;
			(i < x ? w : h) = (i < x ? w : h) * 10 + (text[i] - '0');
		}
		if (w < 1 || h < 1 || w > 16384 || h > 16384)
			return false;
		width = w;
		height = h;
		return true;
	}
} // namespace obui
