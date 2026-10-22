// ob_pcsx2_gs host API: what the embedding app gives the stubbed PCSX2 host layer.
#pragma once

#include "common/WindowInfo.h"

namespace ObPcsx2GS
{
	// Window handed to the GS device by Host::AcquireRenderWindow (stubs/host_stubs.cpp).
	void SetRenderWindow(const WindowInfo& wi);
} // namespace ObPcsx2GS
