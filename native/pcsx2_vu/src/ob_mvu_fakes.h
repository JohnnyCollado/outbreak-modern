// Fakes that replace three PCSX2 globals inside microVU's own text only (ob_mvu_jit.cpp defines
// `gifUnit`, `vu1Thread` and `SysMemory` as macros naming these after PCSX2's headers are included).
// Built with PCSX2 headers (Gif_Unit.h for Gif_Tag / incTag, MTVU.h for VU_Thread, Config.h).
#pragma once

#include "Common.h"
#include "Config.h"
#include "Gif.h"
#include "Gif_Unit.h"
#include "MTVU.h"

#include <atomic>
#include <cstdint>

namespace ObMvu
{
	using Path1Sink = void (*)(void* ctx, const uint8_t* data, uint32_t size);
	void EmitPath1(const uint8_t* data, uint32_t size); // pcsx2_side.cpp: forwards to the sink

	// The part of Gif_Path that mVU_XGKICK_ uses: CopyGSPacketData for the bytes before a 0x4000 wrap.
	struct FakeGifPath
	{
		u8 pending[0x4000];
		u32 pendingSize = 0;
		void CopyGSPacketData(u8* pMem, u32 size, bool /*aligned*/ = false)
		{
			if (pendingSize + size > sizeof(pending))
				size = sizeof(pending) - pendingSize;
			std::memcpy(pending + pendingSize, pMem, size);
			pendingSize += size;
		}
	};

	// Gif_Unit subset used by microVU_Lower.inl:1698-1714 (mVU_XGKICK_) and :1716-1786 (XGKickHack path).
	struct FakeGifUnit
	{
		FakeGifPath gifPath[3];

		// Gif_Unit.h:591-615 (GetGSPacketSize), verbatim except DevCon.
		u32 GetGSPacketSize(GIF_PATH pathIdx, u8* pMem, u32 offset = 0, u32 size = ~0u, bool flush = false)
		{
			u32 memMask = pathIdx ? ~0u : 0x3fffu;
			u32 curSize = 0;
			for (;;)
			{
				Gif_Tag gifTag(&pMem[offset & memMask]);
				incTag(offset, curSize, 16 + gifTag.len); // Tag + Data length
				if (pathIdx == GIF_PATH_1 && curSize >= 0x4000)
				{
					return 0; // Bios does this... (Fixed if you delay vu1's xgkick by 103 vu cycles)
				}
				if (curSize >= size)
					return size;
				if(((flush && gifTag.tag.EOP) || !flush) && (CHECK_XGKICKHACK || !REC_VU1))
				{
					return curSize | ((u32)gifTag.tag.EOP << 31);
				}
				if (gifTag.tag.EOP )
				{
					return curSize;
				}
			}
		}

		// One XGKICK = (optional CopyGSPacketData before the wrap) + TransferGSPacketData: emit as one packet.
		u32 TransferGSPacketData(GIF_TRANSFER_TYPE /*tranType*/, u8* pMem, u32 size, bool /*aligned*/ = false)
		{
			FakeGifPath& p = gifPath[GIF_PATH_1];
			p.CopyGSPacketData(pMem, size);
			EmitPath1(p.pending, p.pendingSize);
			p.pendingSize = 0;
			return size;
		}
	};

	// VU_Thread subset (MTVU is off: THREAD_VU1 = false, so only addresses and no-op calls are used).
	struct FakeVuThread
	{
		alignas(16) VIFregisters vifRegs;
		u32 vuFBRST = 0;
		std::atomic<u32> mtvuInterrupts{0};
		void Open() {}
		bool IsOpen() const { return false; }
		void WaitVU() {}
		void Get_MTVUChanges() {}
	};

	// SysMemory subset used by mVUinit (microVU.cpp:25-26): the VU1 code cache from pcsx2_side.cpp.
	struct FakeSysMemory
	{
		static u8* GetVU1Rec();
		static u8* GetVU1RecEnd();
		static u8* GetVU0Rec();
		static u8* GetVU0RecEnd();
	};

	extern FakeGifUnit g_gifUnit;
	extern FakeVuThread g_vu1Thread;
} // namespace ObMvu
