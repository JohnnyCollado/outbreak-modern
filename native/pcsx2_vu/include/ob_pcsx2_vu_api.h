// ob_pcsx2_vu as a table of function pointers, for a runner that loads the backend at run time (Android:
// dlopen("libob_a64vu.so") in adapter/hook.cpp, so a library that cannot load falls back to the interpreter like
// one that cannot start). The entries are exactly the functions of ob_pcsx2_vu.h; plain types only.
#pragma once

#include "ob_pcsx2_vu.h"

#include <cstdint>

namespace ObPcsx2Vu
{
	constexpr uint32_t kApiVersion = 1;

	struct Api
	{
		uint32_t version;   // kApiVersion of the library
		uint32_t stateSize; // sizeof(State) of the library
		void (*SetFlagHack)(bool on);
		bool (*InitOwned)(char* err, uint32_t errSize);
		uint8_t* (*OwnedData)();
		uint8_t* (*OwnedCode)();
		void (*Shutdown)();
		void (*SetPath1Sink)(Path1Sink sink, void* ctx);
		void (*SetState)(const State& s);
		void (*GetState)(State& s);
		uint32_t (*Tpc)();
		RunResult (*Run)(uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration,
			uint32_t cycles);
		const char* (*Describe)();
	};

	// The table of the functions this binary links itself (no loading).
	inline Api LinkedApi()
	{
		return Api{kApiVersion, static_cast<uint32_t>(sizeof(State)), &SetFlagHack, &InitOwned, &OwnedData, &OwnedCode,
			&Shutdown, &SetPath1Sink, &SetState, &GetState, &Tpc, &Run, &Describe};
	}
} // namespace ObPcsx2Vu

// Exported by libob_a64vu.so (C linkage, so dlsym needs no mangled name): the table, or null when the caller's
// version differs from the library's.
extern "C" {
typedef const ObPcsx2Vu::Api* (*ObPcsx2VuApiFn)(uint32_t callerVersion);
}
#define OB_PCSX2_VU_API_SYMBOL "ob_pcsx2_vu_api"
