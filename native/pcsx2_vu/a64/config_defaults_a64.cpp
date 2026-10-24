// EmuConfig for libob_a64vu.so: ARMSX2's pcsx2/Pcsx2Config.cpp is not built (it pulls CDVD, memcards, Pad, USB,
// audio), so this file owns EmuConfig and the option constructors its layout needs. The three option structs
// microVU reads (SpeedhackOptions, RecompilerOptions, CpuOptions) and the FPCR defaults are copied VERBATIM from
// ARMSX2 90126bd pcsx2/Pcsx2Config.cpp (lines cited); the others keep zero / in-class defaults, as the GS stubs
// do (native/pcsx2_gs/stubs/config_stubs.cpp). ObMvuApplyConfigDefaults() then applies our overrides (study
// research/2026-10-24-armsx2-microvu-feasibility.md section 3). Re-copy on every ARMSX2 bump.
#include "Config.h"
#include "IopThread/IopThreadMode.h"
#include "common/FPControl.h"

// Pcsx2Config.cpp:35-45
// Default EE/VU control registers have exceptions off, DaZ/FTZ, and the rounding mode set to Chop/Zero.
static constexpr FPControlRegister DEFAULT_FPU_FP_CONTROL_REGISTER = FPControlRegister::GetDefault()
																		 .DisableExceptions()
																		 .SetDenormalsAreZero(true)
																		 .SetFlushToZero(true)
																		 .SetRoundMode(FPRoundMode::ChopZero);
static constexpr FPControlRegister DEFAULT_VU_FP_CONTROL_REGISTER = FPControlRegister::GetDefault()
																		.DisableExceptions()
																		.SetDenormalsAreZero(true)
																		.SetFlushToZero(true)
																		.SetRoundMode(FPRoundMode::ChopZero);

// Pcsx2Config.cpp:47
Pcsx2Config EmuConfig;

// EmuFolders::Cache: read only by the on-disk program cache (off).
std::string EmuFolders::Cache;

// Not verbatim: upstream also fills memory cards, RTC, PINE, autosave (Pcsx2Config.cpp, Pcsx2Config()).
Pcsx2Config::Pcsx2Config() { bitset = 0; }

// Pcsx2Config.cpp:402-411
Pcsx2Config::SpeedhackOptions::SpeedhackOptions()
{
	DisableAll();

	// Set recommended speedhacks to enabled by default. They'll still be off globally on resets.
	WaitLoop = true;
	IntcStat = true;
	vuFlagHack = true;
	vu1Instant = true;
}

// Pcsx2Config.cpp:413-423
Pcsx2Config::SpeedhackOptions& Pcsx2Config::SpeedhackOptions::DisableAll()
{
	// bitset covers DynamicEECycleRate along with the rest. DynamicEECycleRateTrace is
	// left alone: where a development trace goes is not a speedhack to turn off.
	bitset = 0;
	EECycleRate = 0;
	EECycleSkip = 0;
	iopThreadLag = IopThread::kDefaultLag;

	return *this;
}

// Pcsx2Config.cpp:478-512
Pcsx2Config::RecompilerOptions::RecompilerOptions()
{
	bitset = 0;

	//StackFrameChecks	= false;
	//PreBlockCheckEE	= false;

	// All recs are enabled by default.

	EnableEE = true;
	EnableEECache = false;
	EnableIOP = true;
	EnableVU0 = true;
	EnableVU1 = true;
	EnableFastmem = true;
	PauseOnTLBMiss = false;
	EnableVUProgramCache = false; // default off; opt-in until the on-disk cache is validated on the target hardware
	EnableVUAot = true; // default on (arm64 builds; the setting is ignored elsewhere); it also turns the VU program cache on

	// vu and fpu clamping default to standard overflow.
	vu0Overflow = true;
	//vu0ExtraOverflow = false;
	//vu0SignOverflow = false;
	//vu0ExactMode = false;
	vu1Overflow = true;
	//vu1ExtraOverflow = false;
	//vu1SignOverflow = false;
	//vu1ExactMode = false;

	fpuOverflow = true;
	//fpuExtraOverflow = false;
	//fpuFullMode = false;
	//fpuExactMode = false;
	fpuGuardedAddSub = true; // PS2-accurate add/sub guard-bit emulation; opt-out for perf on titles verified not to need it.
}

// Pcsx2Config.cpp:649-660
Pcsx2Config::CpuOptions::CpuOptions()
{
	FPUFPCR = DEFAULT_FPU_FP_CONTROL_REGISTER;

	// Rounding defaults to nearest to match old behavior.
	// TODO: Make it default to the same as the rest of the FPU operations, at some point.
	FPUDivFPCR = FPControlRegister(DEFAULT_FPU_FP_CONTROL_REGISTER).SetRoundMode(FPRoundMode::Nearest);

	VU0FPCR = DEFAULT_VU_FP_CONTROL_REGISTER;
	VU1FPCR = DEFAULT_VU_FP_CONTROL_REGISTER;
	ExtraMemory = false;
}

// Pcsx2Config.cpp:1730-1739: all gamefixes are disabled by default.
Pcsx2Config::GamefixOptions::GamefixOptions()
{
	DisableAll();
}

Pcsx2Config::GamefixOptions& Pcsx2Config::GamefixOptions::DisableAll()
{
	bitset = 0;
	return *this;
}

// Layout only (nothing in microVU reads them): zero / in-class defaults.
Pcsx2Config::ProfilerOptions::ProfilerOptions() {}
Pcsx2Config::GSOptions::GSOptions() {}
Pcsx2Config::SavestateOptions::SavestateOptions() {}
Pcsx2Config::FilenameOptions::FilenameOptions() {}
Pcsx2Config::EmulationSpeedOptions::EmulationSpeedOptions() {}
Pcsx2Config::USBOptions::USBOptions() {}
Pcsx2Config::PadOptions::PadOptions() {}
Pcsx2Config::AchievementsOptions::AchievementsOptions() {}
Pcsx2Config::SPU2Options::SPU2Options() {}
Pcsx2Config::DEV9Options::DEV9Options() {}
TraceLogFilters::TraceLogFilters() {}
TraceLogsEE::TraceLogsEE() {}
TraceLogsIOP::TraceLogsIOP() {}
TraceLogsMISC::TraceLogsMISC() {}

// Our overrides (study section 3): the VU1 recompiler on (REC_VU1: XGKICK sizes, study 1.4), MTVU off (one host
// thread runs VU1), vu-aot and the on-disk program cache off (step 1; vu-aot is measured later as an option),
// exact mode off (clamp mode 4 needs the FPU hardware model, stubbed). The constructors above already run for
// EmuConfig at load; this re-applies them so a second InitOwned after Shutdown starts from the same values.
void ObMvuApplyConfigDefaults()
{
	EmuConfig.Speedhacks = Pcsx2Config::SpeedhackOptions();
	EmuConfig.Cpu = Pcsx2Config::CpuOptions();
	EmuConfig.Gamefixes = Pcsx2Config::GamefixOptions();
	EmuConfig.Cpu.Recompiler.EnableVU1 = true;
	EmuConfig.Speedhacks.vuThread = false;
	EmuConfig.Cpu.Recompiler.EnableVUAot = false;
	EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
	EmuConfig.Cpu.Recompiler.vu1ExactMode = false;
}
