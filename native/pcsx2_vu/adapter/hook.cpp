// hook: the PS2Recomp half of the runner's external VU1 (PS2X_VU1=mvu). Implements PS2ExternalVu1
// (runtime/ps2_vu1_external.h) over the plain-types API include/ob_pcsx2_vu.h; PCSX2's microVU itself lives
// behind src/pcsx2_side.cpp (built with PCSX2's headers and flags). The two header sets never meet.
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_capture.h"
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
			ObPcsx2Vu::State& o = m_pending;
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
			if (m_memory)
				ObPcsx2Vu::SetState(m_pending);
			else
				m_hasPending = true; // applied once Init has the VU1 memories (first run)
		}

		void readState(VU1State& out) override
		{
			ObPcsx2Vu::State o{};
			if (m_memory)
				ObPcsx2Vu::GetState(o);
			else
				o = m_pending;
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
			// As VU1Interpreter::finishXgkick (ps2_vu1_core.cpp): capture tap, then PATH1 into the GIF arbiter.
			ps2x::vu1capture::notifyPath1(data, size);
			static_cast<Pcsx2MicroVu1*>(ctx)->m_memory->submitGifPacket(GifPathId::Path1, data, size);
		}

		void bind(PS2Memory& memory)
		{
			if (m_memory == &memory && m_vuData == memory.getVU1Data())
				return;
			if (m_memory)
				fatal("VU1 memory moved after microVU was bound to it");
			uint8_t* data = memory.getVU1Data();
			uint8_t* code = memory.getVU1Code();
			char err[512] = {};
			if (!ObPcsx2Vu::Init(data, code, err, sizeof(err)))
				fatal(err);
			ObPcsx2Vu::SetPath1Sink(&Path1, this);
			m_memory = &memory;
			m_vuData = data;
			if (m_hasPending)
				ObPcsx2Vu::SetState(m_pending);
			m_hasPending = false;
			std::printf("[vu1] mvu: %s\n", ObPcsx2Vu::Describe());
			std::fflush(stdout);
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

		PS2Memory* m_memory = nullptr;
		uint8_t* m_vuData = nullptr;
		ObPcsx2Vu::State m_pending{};
		bool m_hasPending = false;
		uint32_t m_unfinished = 0;
	};
} // namespace

PS2ExternalVu1* ps2CreateExternalVu1(const char* mode)
{
	const std::string m = mode ? mode : "";
	if (m != "mvu")
		return nullptr;
	return new Pcsx2MicroVu1();
}
