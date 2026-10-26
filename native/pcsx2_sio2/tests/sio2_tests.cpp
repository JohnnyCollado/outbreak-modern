// ob_pcsx2_sio2 tests (ob.sh test): PCSX2's SIO2 and memory cards behind ob_sio2_bridge.h, without the IOP.
// Commands are sent as sio2man sends them: a queue entry (port | length << 8), the bytes through DATAIN or DMA 11,
// the reply through the FIFO or DMA 12 (byte layout: PCSX2 Sio2::Memcard and MemoryCardProtocol).
// - an empty folder gets a PCSX2-size slot 1 card (8,650,752 bytes) and no slot 2;
// - the slot 1 card answers a probe, the empty slot 2 does not;
// - a sector written through the SIO2 is in the file at once (the runner ends with _Exit) and reads back,
//   also after closing and opening the cards again;
// - a CTRL start raises the SIO2 interrupt once; DMA 11/12 carry a command and its reply.
#include "ob_sio2_bridge.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

static int s_fail = 0, s_total = 0;
static void Check(bool ok, const char* what)
{
	++s_total;
	if (!ok)
	{
		++s_fail;
		std::printf("FAIL: %s\n", what);
	}
}

constexpr uint32_t kQueue0 = 0x1F808200u, kDataIn = 0x1F808260u, kFifo = 0x1F808264u, kCtrl = 0x1F808268u;
constexpr uint32_t kPageRaw = 528u; // 512 data + 16 ECC bytes per sector in a .ps2 file

// One SIO2 command by register writes; returns the reply bytes (as many as were sent).
static std::vector<uint8_t> Send(uint32_t port, const std::vector<uint8_t>& bytes)
{
	ob_sio2_write32(kQueue0, port | (static_cast<uint32_t>(bytes.size()) << 8));
	ob_sio2_write32(kQueue0 + 4, 0); // end of the queue
	for (uint8_t b : bytes)
		ob_sio2_write8(kDataIn, b);
	std::vector<uint8_t> reply(bytes.size());
	for (uint8_t& b : reply)
		b = ob_sio2_read8(kFifo);
	return reply;
}

static std::vector<uint8_t> SetSector(uint8_t command, uint32_t sector)
{
	const uint8_t s0 = sector & 0xFF, s1 = (sector >> 8) & 0xFF, s2 = (sector >> 16) & 0xFF, s3 = sector >> 24;
	return {0x81, command, s0, s1, s2, s3, static_cast<uint8_t>(s0 ^ s1 ^ s2 ^ s3), 0, 0};
}

int main()
{
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ob_sio2_tests";
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	const std::filesystem::path card = dir / "Mcd001.ps2";

	Check(!ob_sio2_open("", true), "no folder: no cards");
	// Without create_slot1 (PS2X_SAVES) an empty folder is "no card": nothing created, the probe gets no answer.
	Check(ob_sio2_open(dir.string().c_str(), false), "open an empty folder as found");
	Check(!std::filesystem::exists(card) && !ob_sio2_card_present(0), "no card made, slot 1 empty");
	const std::vector<uint8_t> none = Send(0, {0x81, 0x11, 0, 0});
	Check(none[2] == 0xFF && none[3] == 0xFF, "the empty slot 1 does not answer the probe");
	ob_sio2_close();
	Check(ob_sio2_open(dir.string().c_str(), true), "open");
	Check(std::filesystem::file_size(card, ec) == 8650752u, "slot 1 card created at PCSX2's 8 MB + ECC size");
	Check(!std::filesystem::exists(dir / "Mcd002.ps2"), "no slot 2 card made");
	Check(ob_sio2_card_present(0) && !ob_sio2_card_present(1), "card present in slot 1 only");

	const std::vector<uint8_t> probe = Send(0, {0x81, 0x11, 0, 0});
	Check(probe[2] == 0x2B && probe[3] == 0x55, "slot 1 answers the probe (0x2B, terminator 0x55)");
	const std::vector<uint8_t> probe2 = Send(1, {0x81, 0x11, 0, 0});
	Check(probe2[2] == 0xFF && probe2[3] == 0xFF, "the empty slot 2 does not answer");

	// Write 16 bytes at the start of sector 5, then read them back.
	std::vector<uint8_t> data(16);
	for (size_t i = 0; i < data.size(); i++)
		data[i] = static_cast<uint8_t>(0xA0 + i);
	Send(0, SetSector(0x22, 5));
	std::vector<uint8_t> write = {0x81, 0x42, static_cast<uint8_t>(data.size())};
	write.insert(write.end(), data.begin(), data.end());
	write.insert(write.end(), {0, 0, 0}); // n + 6 bytes: as many as the reply (00 00 00 2B, n zeros, checksum, terminator)
	Send(0, write);
	{
		std::ifstream f(card, std::ios::binary);
		f.seekg(5 * kPageRaw);
		std::vector<uint8_t> onDisk(data.size());
		f.read(reinterpret_cast<char*>(onDisk.data()), static_cast<std::streamsize>(onDisk.size()));
		Check(f && onDisk == data, "a written sector is in the card file at once");
	}
	auto readBack = [&]() {
		Send(0, SetSector(0x23, 5));
		std::vector<uint8_t> read = {0x81, 0x43, static_cast<uint8_t>(data.size())};
		read.resize(read.size() + data.size() + 3, 0); // n + 6, as the reply (00 00 00 2B, data, checksum, terminator)
		const std::vector<uint8_t> reply = Send(0, read);
		return std::vector<uint8_t>(reply.begin() + 4, reply.begin() + 4 + static_cast<long>(data.size()));
	};
	Check(readBack() == data, "the sector reads back through the SIO2");

	Check(!ob_sio2_take_interrupt(), "no interrupt before a transfer starts");
	ob_sio2_write32(kCtrl, 0x3BD); // sio2man's CTRL value | START_TRANSFER
	Check(ob_sio2_take_interrupt() && !ob_sio2_take_interrupt(), "a CTRL start raises the SIO2 interrupt once");
	ob_sio2_write32(0x1F808280u, 1); // acknowledge (ISTAT write clears), as sio2man's handler does
	ob_sio2_write32(kCtrl, 0x3BD);
	Check(ob_sio2_take_interrupt(), "after the acknowledge the next start interrupts again");
	ob_sio2_write32(0x1F808280u, 1);

	// The probe again over DMA: 1 block of 1 word (4 bytes) in, the 4-byte reply out.
	std::vector<uint8_t> ram(0x10000, 0);
	const uint8_t probeBytes[4] = {0x81, 0x11, 0, 0};
	std::memcpy(&ram[0x100], probeBytes, 4);
	ob_sio2_write32(kQueue0, 0 | (4u << 8));
	ob_sio2_write32(kQueue0 + 4, 0);
	const uint32_t inDelay = ob_sio2_dma(11, ram.data(), static_cast<uint32_t>(ram.size()), 0x100, (1u << 16) | 1u, 0x01000201);
	const uint32_t outDelay = ob_sio2_dma(12, ram.data(), static_cast<uint32_t>(ram.size()), 0x200, (1u << 16) | 1u, 0x41000200);
	Check(inDelay != OB_SIO2_DMA_IGNORED && outDelay != OB_SIO2_DMA_IGNORED, "DMA 11/12 take sio2man's CHCRs");
	Check(ram[0x202] == 0x2B && ram[0x203] == 0x55, "the probe over DMA 11/12 answers");
	Check(ob_sio2_dma(12, ram.data(), static_cast<uint32_t>(ram.size()), 0x200, 1, 0x01000201) == OB_SIO2_DMA_IGNORED,
		"DMA 12 ignores a CHCR psxDma12 does not take");

	ob_sio2_close();
	Check(ob_sio2_open(dir.string().c_str(), false), "reopen as found: the card is used, not replaced");
	Check(readBack() == data, "the sector is still there after closing and opening the cards");
	ob_sio2_close();
	std::filesystem::remove_all(dir, ec);

	std::printf("pcsx2_sio2: %d/%d\n", s_total - s_fail, s_total);
	return s_fail ? 1 : 0;
}
