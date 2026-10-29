// safe_text: text from files (memory card save names, ini values) made safe for Android's JNI NewStringUTF, which
// takes Modified UTF-8 only and aborts a debuggable app on anything else (security queue C5,
// docs/plans/2026-10-29-security-queue.md). Valid 1-3 byte UTF-8 is kept; invalid bytes, NUL, encoded surrogates
// and 4-byte characters (which Modified UTF-8 writes as surrogate pairs) become '?'.
#pragma once

#include <string>

namespace obui
{
	std::string javaSafeText(const std::string& bytes);
} // namespace obui
