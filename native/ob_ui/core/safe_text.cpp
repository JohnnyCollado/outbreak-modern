#include "safe_text.h"

namespace obui
{
	std::string javaSafeText(const std::string& bytes)
	{
		std::string out;
		out.reserve(bytes.size());
		const size_t n = bytes.size();
		auto at = [&](size_t i) { return static_cast<unsigned char>(bytes[i]); };
		auto cont = [&](size_t i) { return i < n && (at(i) & 0xC0) == 0x80; };
		for (size_t i = 0; i < n;)
		{
			const unsigned char c = at(i);
			if (c >= 0x01 && c < 0x80)
			{
				out.push_back(static_cast<char>(c));
				i++;
			}
			else if (c >= 0xC2 && c <= 0xDF && cont(i + 1))
			{
				out.append(bytes, i, 2);
				i += 2;
			}
			else if (c >= 0xE0 && c <= 0xEF && cont(i + 1) && cont(i + 2))
			{
				const unsigned char c1 = at(i + 1);
				const bool overlong = c == 0xE0 && c1 < 0xA0;
				const bool surrogate = c == 0xED && c1 >= 0xA0;
				if (overlong || surrogate)
				{
					out.push_back('?');
					i++;
					continue;
				}
				out.append(bytes, i, 3);
				i += 3;
			}
			else if (c >= 0xF0 && c <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3))
			{
				out.push_back('?'); // a whole 4-byte character: Modified UTF-8 would need a surrogate pair
				i += 4;
			}
			else
			{
				out.push_back('?'); // NUL, a lone continuation, an invalid lead or a cut-off sequence: one byte
				i++;
			}
		}
		return out;
	}
} // namespace obui
