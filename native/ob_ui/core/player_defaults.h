// player_defaults: what a person double-clicking the .exe gets, with no environment set (report entry 69). Scripted
// runs say what they want (PS2X_GS, a hidden window, the launcher off), so the gate's defaults stay as they are.
#pragma once

#include <cstring>
#include <string>

namespace obui
{
	inline bool playerWantsVulkan(const char* gsEnv, const char* hiddenEnv, const char* launcherEnv)
	{
		if (gsEnv && *gsEnv)
			return false;
		if (hiddenEnv && std::strcmp(hiddenEnv, "1") == 0)
			return false;
		return !(launcherEnv && std::strcmp(launcherEnv, "0") == 0);
	}

	// vu1Setting: the Advanced page's choice ("" = Default).
	inline bool playerWantsMvu(const char* vu1Env, const std::string& vu1Setting)
	{
		return !(vu1Env && *vu1Env) && vu1Setting.empty();
	}
} // namespace obui
