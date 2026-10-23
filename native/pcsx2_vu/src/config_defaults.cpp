// PCSX2 config defaults that microVU reads (CHECK_VU_OVERFLOW, vuFlagHack, VU1FPCR, ...).
// ob_pcsx2_gs (native/pcsx2_gs/stubs/config_stubs.cpp) owns EmuConfig and gives these option structs
// EMPTY constructors, so without this every clamp mode, speed hack and FPCR would be zero. We do not edit
// those stubs (the GS gate was scored with them); instead each struct below derives from the PCSX2 one and
// runs the constructor body copied VERBATIM from PCSX2 v2.9.134 pcsx2/Pcsx2Config.cpp (lines cited), and
// ObMvuApplyConfigDefaults() assigns the result into EmuConfig. Re-copy on every PCSX2 bump.
#include "Config.h"
#include "common/FPControl.h"

// Pcsx2Config.cpp:31-41
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

// Pcsx2Config.cpp:389-396 (DisableAll is declared in Config.h and defined only in Pcsx2Config.cpp, which
// neither library builds).
Pcsx2Config::SpeedhackOptions& Pcsx2Config::SpeedhackOptions::DisableAll()
{
	bitset = 0;
	EECycleRate = 0;
	EECycleSkip = 0;

	return *this;
}

namespace
{
	struct ObSpeedhackOptions : Pcsx2Config::SpeedhackOptions
	{
		// Pcsx2Config.cpp:378-387
		ObSpeedhackOptions()
		{
			DisableAll();

			// Set recommended speedhacks to enabled by default. They'll still be off globally on resets.
			WaitLoop = true;
			IntcStat = true;
			vuFlagHack = true;
			vu1Instant = true;
		}
	};

	struct ObRecompilerOptions : Pcsx2Config::RecompilerOptions
	{
		// Pcsx2Config.cpp:445-470
		ObRecompilerOptions()
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

			// vu and fpu clamping default to standard overflow.
			vu0Overflow = true;
			//vu0ExtraOverflow = false;
			//vu0SignOverflow = false;
			//vu0Underflow = false;
			vu1Overflow = true;
			//vu1ExtraOverflow = false;
			//vu1SignOverflow = false;
			//vu1Underflow = false;

			fpuOverflow = true;
			//fpuExtraOverflow = false;
			//fpuFullMode = false;
		}
	};

	struct ObCpuOptions : Pcsx2Config::CpuOptions
	{
		// Pcsx2Config.cpp:596-605
		ObCpuOptions()
		{
			FPUFPCR = DEFAULT_FPU_FP_CONTROL_REGISTER;

			// Rounding defaults to nearest to match old behavior.
			// TODO: Make it default to the same as the rest of the FPU operations, at some point.
			FPUDivFPCR = FPControlRegister(DEFAULT_FPU_FP_CONTROL_REGISTER).SetRoundMode(FPRoundMode::Nearest);

			VU0FPCR = DEFAULT_VU_FP_CONTROL_REGISTER;
			VU1FPCR = DEFAULT_VU_FP_CONTROL_REGISTER;
			ExtraMemory = false;
		}
	};
} // namespace

// Gamefixes: GamefixOptions() is DisableAll() = all off upstream; the stub leaves the zero-initialised
// global, the same value. No Outbreak release has a VU gamefix in GameIndex.yaml (study section 3).
void ObMvuApplyConfigDefaults()
{
	static_cast<Pcsx2Config::SpeedhackOptions&>(EmuConfig.Speedhacks) = ObSpeedhackOptions();
	ObCpuOptions cpu;
	cpu.Recompiler = ObRecompilerOptions();
	static_cast<Pcsx2Config::CpuOptions&>(EmuConfig.Cpu) = cpu;
	EmuConfig.Gamefixes.bitset = 0;
}
