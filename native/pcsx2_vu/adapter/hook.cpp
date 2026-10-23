// hook: the PS2Recomp half of the runner's external VU1 (PS2X_VU1=mvu). Implements PS2ExternalVu1
// (runtime/ps2_vu1_external.h) over the plain-types API include/ob_pcsx2_vu.h; PCSX2's microVU itself lives
// behind src/pcsx2_side.cpp (built with PCSX2's headers and flags). The two header sets never meet.
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_diag_hooks.h"
#include "runtime/ps2_vu1_external.h"

#include "ob_pcsx2_vu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	uint32_t FloatBits(float f)
	{
		uint32_t u;
		std::memcpy(&u, &f, sizeof(u));
		return u;
	}

	float BitsFloat(uint32_t u)
	{
		float f;
		std::memcpy(&f, &u, sizeof(f));
		return f;
	}

	class Pcsx2MicroVu1 final : public PS2ExternalVu1
	{
	public:
		PS2Vu1RunResult mscal(PS2Memory& memory, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst) override
		{
			return run(memory, startPC, top, itop, fbrst);
		}

		PS2Vu1RunResult mscnt(PS2Memory& memory, uint32_t top, uint32_t itop, uint32_t fbrst) override
		{
			bind(memory);
			return run(memory, ObPcsx2Vu::Tpc(), top, itop, fbrst);
		}

		void setState(const VU1State& s) override
		{
			ObPcsx2Vu::State o{};
			for (int r = 0; r < 32; ++r)
				for (int l = 0; l < 4; ++l)
					o.vf[r][l] = FloatBits(s.vf[r][l]);
			for (int r = 0; r < 16; ++r)
				o.vi[r] = static_cast<uint32_t>(s.vi[r]) & 0xFFFFu;
			for (int l = 0; l < 4; ++l)
				o.acc[l] = FloatBits(s.acc[l]);
			o.q = FloatBits(s.q);
			o.p = FloatBits(s.p);
			o.i = FloatBits(s.i);
			o.r = s.r;
			o.status = s.status;
			o.mac = s.mac;
			o.clip = s.clip;
			o.tpc = s.pc & 0x3FF8u;
			ObPcsx2Vu::SetState(o); // microVU is up from creation (InitOwned)
		}

		void readState(VU1State& out) override
		{
			ObPcsx2Vu::State o{};
			ObPcsx2Vu::GetState(o);
			out = VU1State{};
			for (int r = 0; r < 32; ++r)
				for (int l = 0; l < 4; ++l)
					out.vf[r][l] = BitsFloat(o.vf[r][l]);
			for (int r = 0; r < 16; ++r)
				out.vi[r] = static_cast<int16_t>(o.vi[r] & 0xFFFFu); // sign-extended, as the interpreter stores VI
			for (int l = 0; l < 4; ++l)
				out.acc[l] = BitsFloat(o.acc[l]);
			out.q = BitsFloat(o.q);
			out.p = BitsFloat(o.p);
			out.i = BitsFloat(o.i);
			out.r = o.r;
			out.status = o.status;
			out.mac = o.mac;
			out.clip = o.clip;
			out.pc = o.tpc;
		}

	private:
		static void Path1(void* ctx, const uint8_t* data, uint32_t size)
		{
			// As VU1Interpreter::finishXgkick (ps2_vu1_core.cpp): diagnostics hook, then PATH1 into the GIF arbiter.
			if (PS2DiagHooks* diag = ps2DiagHooks())
				diag->onVu1Path1(data, size);
			static_cast<Pcsx2MicroVu1*>(ctx)->m_memory->submitGifPacket(GifPathId::Path1, data, size);
		}

		// The memory the XGKICK packets go to. microVU already runs on the VU1 memories it owns, which
		// PS2Memory::initialize took through vu1Memory (patch 0041), so there is nothing to place here.
		void bind(PS2Memory& memory)
		{
			if (m_memory == &memory)
				return;
			if (memory.getVU1Data() != ObPcsx2Vu::OwnedData() || memory.getVU1Code() != ObPcsx2Vu::OwnedCode())
				fatal("PS2Memory does not use the backend's VU1 memory (PS2Memory initialized before the backend was set?)");
			m_memory = &memory;
		}

		PS2Vu1RunResult run(PS2Memory& memory, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst)
		{
			bind(memory);
			// PCSX2's own per-call budget (vu1RunCycles, VUmicro.h:20); the interpreter's is 65536.
			const ObPcsx2Vu::RunResult r =
				ObPcsx2Vu::Run(startPC, top, itop, fbrst, memory.getVU1CodeGeneration(), 3000000u);
			if (!r.ended && m_unfinished++ < 8u)
			{
				std::fprintf(stderr, "[vu1] mvu: program at 0x%04x did not reach its E bit (%u cycles)\n", startPC,
					r.mvuCycles);
				std::fflush(stderr);
			}
			PS2Vu1RunResult result;
			result.stoppedByD = false; // microVU compiles D-bit handling out (doDBitHandling = false, microVU_Misc.h)
			result.stoppedByT = r.stoppedByT;
			return result;
		}

		[[noreturn]] static void fatal(const char* msg)
		{
			std::fprintf(stderr, "[vu1] backend mvu failed: %s\n", msg);
			std::fflush(stderr);
			std::_Exit(2);
		}

	public:
		bool vu1Memory(uint8_t** code, uint8_t** data) override
		{
			*code = ObPcsx2Vu::OwnedCode();
			*data = ObPcsx2Vu::OwnedData();
			return *code && *data;
		}

		Pcsx2MicroVu1() { ObPcsx2Vu::SetPath1Sink(&Path1, this); }

	private:
		PS2Memory* m_memory = nullptr;
		uint32_t m_unfinished = 0;
	};
} // namespace

// At startup, before the window, the GPU driver and PS2Memory exist: microVU comes up here on memory it owns
// (InitOwned: VU1 data, code and the 64 MB code cache in one reservation), so a failure is reported now and
// the runtime falls back to the interpreter (ps2InstallExternalVu1) before any VU1 program has run.
PS2ExternalVu1* ps2CreateExternalVu1(const char* mode, char* err, uint32_t errSize)
{
	auto fail = [&](const char* msg) -> PS2ExternalVu1* {
		if (err && errSize)
			std::snprintf(err, errSize, "%s", msg);
		return nullptr;
	};
	const std::string m = mode ? mode : "";
	if (m != "mvu")
		return fail("ob_pcsx2_vu: unknown mode");
	char why[512] = {};
	if (!ObPcsx2Vu::InitOwned(why, sizeof(why)))
		return fail(why);
	std::fprintf(stderr, "[vu1] mvu active: %s\n", ObPcsx2Vu::Describe());
	std::fflush(stderr);
	return new Pcsx2MicroVu1();
}
