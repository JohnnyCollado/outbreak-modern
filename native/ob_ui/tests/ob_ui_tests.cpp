// ob_ui_core tests (ob.sh test): the settings menu's platform-free part.
// - IniFile: an unchanged file saves byte-identical; set replaces in place, lands after its commented example, adds to
//   a section or a new one; input.ini inline comments and their column stay; CRLF stays; quotes;
// - disc check: SHA-256 known vectors, the PCSX2 CRC, a synthetic ISO (Ok, another release named by its CRC, another
//   game, not a disc image, no file), an extracted folder and a bare ELF;
// - menu trigger: F1/Esc, the Back+Start combo after 1 s, hold-back of a lone Start, a combo hidden from the game,
//   a late tap, the pad gate while open and after closing;
// - memory cards: unformatted, a formatted card with one save, transfer never overwrites, backups keep N;
// - touch pad: touch buttons press on top of the host pad, a pushed touch stick wins, a centred one leaves the
//   physical stick, physical use is counted (buttons, a stick past its threshold) and touch alone is not;
// - game screen: menu layout before the game task and while the menu byte is set, play layout otherwise, the
//   controller type, the attached RAM read live;
// - network: strict addresses, the game's settings from the Network page's choices, no injection.
#include "disc_check.h"
#include "game_screen.h"
#include "ini_file.h"
#include "memory_card.h"
#include "menu_trigger.h"
#include "net_choice.h"
#include "safe_text.h"
#include "window_size.h"
#include "touch_pad.h"
#include "widescreen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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

namespace fs = std::filesystem;

static fs::path TempDir(const char* name)
{
	const fs::path d = fs::temp_directory_path() / "ob_ui_tests" / name;
	std::error_code ec;
	fs::remove_all(d, ec);
	fs::create_directories(d, ec);
	return d;
}

static void WriteBytes(const fs::path& p, const std::vector<uint8_t>& b)
{
	std::ofstream f(p, std::ios::binary);
	f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

// ---- IniFile ----
static void IniTests()
{
	const std::string outbreak =
		"; outbreak.ini\n"
		"game = D:\\Games\\a;b.iso\n"
		"\n"
		"; upscale: render resolution multiplier\n"
		"; upscale = 3\n"
		"\n"
		"; window = 1440x810\n";
	obui::IniFile ini;
	ini.parse(outbreak);
	Check(ini.text() == outbreak, "ini: unchanged text is byte-identical");
	Check(ini.get("", "GAME").value_or("") == "D:\\Games\\a;b.iso", "ini: ';' inside an outbreak.ini value is kept");
	Check(!ini.get("", "upscale"), "ini: a commented example is not a value");
	ini.set("", "game", "E:\\new.iso");
	Check(ini.get("", "game").value_or("") == "E:\\new.iso", "ini: set replaces the value");
	ini.set("", "upscale", "4");
	{
		const std::string t = ini.text();
		Check(t.find("; upscale = 3\nupscale = 4\n") != std::string::npos, "ini: new key lands after its commented example");
		Check(t.find("; outbreak.ini\ngame = E:\\new.iso\n") == 0, "ini: other lines untouched");
	}
	ini.set("", "saves", "D:\\saves");
	Check(ini.text().find("; window = 1440x810\nsaves = D:\\saves\n") != std::string::npos,
		"ini: new key without example goes at the end of its section");
	ini.set("", "mods", " padded ");
	Check(ini.text().find("mods = \" padded \"") != std::string::npos && ini.get("", "mods").value_or("") == " padded ",
		"ini: quotes for leading/trailing spaces, stripped by get");
	Check(ini.remove("", "mods") && !ini.get("", "mods"), "ini: remove");

	// Last active line wins; keys case-insensitive.
	obui::IniFile dup;
	dup.parse("vu1 = interp\nVU1 = mvu\n");
	Check(dup.get("", "vu1").value_or("") == "mvu", "ini: last line wins");
	dup.set("", "vu1", "interp");
	Check(dup.text() == "vu1 = interp\nVU1 = interp\n", "ini: set changes the last line");

	// CRLF preserved, and used for new lines.
	obui::IniFile crlf;
	crlf.parse("a = 1\r\nb = 2\r\n");
	crlf.set("", "b", "3");
	crlf.set("", "c", "4");
	Check(crlf.text() == "a = 1\r\nb = 3\r\nc = 4\r\n", "ini: CRLF kept");
	obui::IniFile noEol;
	noEol.parse("a = 1");
	noEol.set("", "b", "2");
	Check(noEol.text() == "a = 1\nb = 2\n", "ini: last line without newline gets one");

	// input.ini: sections, inline comments and their column.
	const std::string input =
		"[keyboard]\n"
		"cross    = K            ; Cross = K\n"
		"circle   = L            ; Circle = L\n"
		"\n"
		"[gamepad]\n"
		"cross    = RIGHT_FACE_DOWN  ; A\n";
	obui::IniFile in(true);
	in.parse(input);
	Check(in.text() == input, "input.ini: unchanged text is byte-identical");
	Check(in.get("keyboard", "cross").value_or("") == "K", "input.ini: inline comment stripped");
	Check(in.get("GAMEPAD", "cross").value_or("") == "RIGHT_FACE_DOWN", "input.ini: section lookup");
	in.set("keyboard", "cross", "SPACE");
	Check(in.text().find("cross    = SPACE        ; Cross = K\n") != std::string::npos, "input.ini: comment column kept");
	in.set("keyboard", "cross", "A_VERY_LONG_KEY_NAME_HERE");
	Check(in.text().find("cross    = A_VERY_LONG_KEY_NAME_HERE ; Cross = K\n") != std::string::npos,
		"input.ini: long value, comment after one space");
	in.set("keyboard", "square", "J");
	Check(in.text().find("circle   = L            ; Circle = L\nsquare = J\n\n[gamepad]") != std::string::npos,
		"input.ini: new key at the end of its section, before the blank line");
	in.set("mouse", "look", "on");
	Check(in.text().find("[gamepad]\ncross    = RIGHT_FACE_DOWN  ; A\n\n[mouse]\nlook = on\n") != std::string::npos,
		"input.ini: new section at the end");

	// Save / load round trip through a temp file.
	const fs::path dir = TempDir("ini");
	obui::IniFile missing;
	Check(missing.load(dir / "none.ini") && missing.text().empty(), "ini: missing file loads empty");
	Check(in.save(dir / "input.ini"), "ini: save");
	obui::IniFile back(true);
	Check(back.load(dir / "input.ini") && back.text() == in.text(), "ini: load returns what was saved");
	Check(!fs::exists(dir / "input.ini.tmp"), "ini: no tmp file left");
}

// ---- disc check ----
static void PutLe32(std::vector<uint8_t>& b, size_t at, uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

// One ISO 9660 directory record.
static std::vector<uint8_t> DirRecord(const std::string& name, uint32_t lba, uint32_t size, bool dir)
{
	const size_t nameLen = name.empty() ? 1 : name.size();
	size_t len = 33 + nameLen;
	if (len & 1)
		++len;
	std::vector<uint8_t> r(len, 0);
	r[0] = static_cast<uint8_t>(len);
	PutLe32(r, 2, lba);
	PutLe32(r, 10, size);
	r[25] = dir ? 2 : 0;
	r[32] = static_cast<uint8_t>(nameLen);
	if (name.empty())
		r[33] = 0;
	else
		std::memcpy(&r[33], name.data(), name.size());
	return r;
}

// PVD at 16, root directory at 18, SYSTEM.CNF at 19, the boot file from 20.
static std::vector<uint8_t> MakeIso(const std::string& cnf, const std::string& bootName, const std::vector<uint8_t>& elf)
{
	const uint32_t S = 2048, elfSectors = static_cast<uint32_t>((elf.size() + S - 1) / S);
	std::vector<uint8_t> iso((20 + elfSectors + 1) * S, 0);
	uint8_t* pvd = &iso[16 * S];
	pvd[0] = 1;
	std::memcpy(pvd + 1, "CD001", 5);
	std::vector<uint8_t> dir;
	auto add = [&](const std::vector<uint8_t>& r) { dir.insert(dir.end(), r.begin(), r.end()); };
	add(DirRecord("", 18, S, true));
	add(DirRecord("\x01", 18, S, true));
	add(DirRecord("SYSTEM.CNF;1", 19, static_cast<uint32_t>(cnf.size()), false));
	add(DirRecord(bootName + ";1", 20, static_cast<uint32_t>(elf.size()), false));
	std::memcpy(&iso[18 * S], dir.data(), dir.size());
	const std::vector<uint8_t> rootRec = DirRecord("", 18, S, true);
	std::memcpy(pvd + 156, rootRec.data(), 34);
	std::memcpy(&iso[19 * S], cnf.data(), cnf.size());
	std::memcpy(&iso[20 * S], elf.data(), elf.size());
	return iso;
}

static void DiscTests()
{
	Check(obui::sha256Hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256: empty");
	Check(obui::sha256Hex(reinterpret_cast<const uint8_t*>("abc"), 3) ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256: abc");
	{
		const std::string s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // 56 bytes: two padding blocks
		Check(obui::sha256Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()) ==
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256: 56-byte vector");
		std::vector<uint8_t> million(1000000, 'a');
		Check(obui::sha256Hex(million.data(), million.size()) ==
			"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256: one million 'a'");
	}
	{
		const uint8_t w[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80};
		Check(obui::pcsx2ElfCrc(w, 8) == 0x80000001u, "crc: XOR of little-endian words");
	}
	{
		std::string boot, serial, ver;
		Check(obui::parseSystemCnf("BOOT2 = cdrom0:\\SLPM_654.28;1\r\nVER = 1.04\r\nVMODE = NTSC\r\n", boot, serial, ver) &&
			boot == "SLPM_654.28" && serial == "SLPM-65428" && ver == "1.04", "SYSTEM.CNF: boot file, serial, version");
	}

	std::vector<uint8_t> elf(6000);
	elf[0] = 0x7F; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
	for (size_t i = 4; i < elf.size(); ++i)
		elf[i] = static_cast<uint8_t>(i * 7);
	obui::ExpectedElf expected{"SLPM-65428", elf.size(), obui::sha256Hex(elf.data(), elf.size()), obui::pcsx2ElfCrc(elf.data(), elf.size())};
	const std::string cnf = "BOOT2 = cdrom0:\\SLPM_654.28;1\nVER = 1.04\n";
	const fs::path dir = TempDir("disc");

	WriteBytes(dir / "good.iso", MakeIso(cnf, "SLPM_654.28", elf));
	obui::DiscReport r = obui::checkDisc(dir / "good.iso", expected);
	Check(r.verdict == obui::DiscVerdict::Ok && r.stepsPassed == 3 && r.serial == "SLPM-65428" && r.version == "1.04",
		"disc: matching image verified");

	obui::ExpectedElf other = expected;
	other.sha256 = std::string(64, '0');
	std::vector<uint8_t> jp = elf;
	// Make the image's ELF have the original Japanese disc's CRC: fix the last word so the XOR comes out 0x03C8F393.
	PutLe32(jp, jp.size() - 4, 0);
	PutLe32(jp, jp.size() - 4, obui::pcsx2ElfCrc(jp.data(), jp.size()) ^ 0x03C8F393u);
	WriteBytes(dir / "jp.iso", MakeIso(cnf, "SLPM_654.28", jp));
	r = obui::checkDisc(dir / "jp.iso", expected);
	Check(r.verdict == obui::DiscVerdict::WrongRelease && r.stepsPassed == 2 && r.crc == 0x03C8F393u &&
		r.message.find("original Japanese disc") != std::string::npos, "disc: another release named by its CRC");

	WriteBytes(dir / "other.iso", MakeIso("BOOT2 = cdrom0:\\SLUS_202.28;1\n", "SLUS_202.28", elf));
	r = obui::checkDisc(dir / "other.iso", expected);
	Check(r.verdict == obui::DiscVerdict::WrongGame && r.stepsPassed == 1 && r.message.find("SLUS-20228") != std::string::npos,
		"disc: another game named by its serial");

	WriteBytes(dir / "garbage.iso", std::vector<uint8_t>(100000, 0x5A));
	r = obui::checkDisc(dir / "garbage.iso", expected);
	Check(r.verdict == obui::DiscVerdict::NotDiscImage && r.stepsPassed == 0, "disc: not a disc image");
	r = obui::checkDisc(dir / "missing.iso", expected);
	Check(r.verdict == obui::DiscVerdict::NoFile && r.stepsPassed == 0, "disc: no file");
	r = obui::checkDisc(fs::path(), expected);
	Check(r.verdict == obui::DiscVerdict::NoFile, "disc: nothing chosen");

	// An extracted folder and the bare ELF (outbreak.ini / command line forms).
	fs::create_directories(dir / "folder");
	WriteBytes(dir / "folder" / "SYSTEM.CNF", std::vector<uint8_t>(cnf.begin(), cnf.end()));
	WriteBytes(dir / "folder" / "SLPM_654.28", elf);
	r = obui::checkDisc(dir / "folder", expected);
	Check(r.verdict == obui::DiscVerdict::Ok && r.stepsPassed == 3, "disc: extracted folder verified");
	r = obui::checkDisc(dir / "folder" / "SLPM_654.28", expected);
	Check(r.verdict == obui::DiscVerdict::Ok && r.stepsPassed == 3, "disc: bare boot ELF verified");

	// A disc held as a descriptor ("/proc/self/fd/<n>", Android's picker): checked through the descriptor, never
	// reopened by name (which Android 16 refuses for picker-granted files). On Windows no such path exists, so a
	// pass here proves the descriptor is used.
	{
#if defined(_WIN32)
		const int fd = ::_wopen((dir / "good.iso").c_str(), _O_RDONLY | _O_BINARY);
#else
		const int fd = ::open((dir / "good.iso").c_str(), O_RDONLY);
#endif
		Check(fd >= 0, "disc: test descriptor opened");
		r = obui::checkDisc(fs::path("/proc/self/fd/" + std::to_string(fd)), expected);
		Check(r.verdict == obui::DiscVerdict::Ok && r.stepsPassed == 3, "disc: a held descriptor (/proc/self/fd/<n>) verified");
		r = obui::checkDisc(fs::path("/proc/self/fd/" + std::to_string(fd)), expected);
		Check(r.verdict == obui::DiscVerdict::Ok, "disc: the same descriptor checks twice (offset shared, reads seek)");
#if defined(_WIN32)
		::_close(fd);
#else
		::close(fd);
#endif
#if !defined(_WIN32) // the MSVC CRT's _dup of a closed descriptor calls the invalid-parameter handler (ends the run)
		r = obui::checkDisc(fs::path("/proc/self/fd/99999"), expected);
		Check(r.verdict == obui::DiscVerdict::ReadError, "disc: a descriptor that isn't open = can't be opened");
#endif
	}

	obui::DiscCache cache;
	cache.remember(dir / "good.iso", obui::checkDisc(dir / "good.iso", expected));
	Check(cache.sameFileAs(dir / "good.iso") && !cache.sameFileAs(dir / "jp.iso"), "disc cache: same file recognised");

	// Optional, by hand: OB_UI_REAL_DISC=<the player's .iso, folder or ELF> must pass against the built-in ELF.
	if (const char* real = std::getenv("OB_UI_REAL_DISC"); real && *real)
	{
		r = obui::checkDisc(fs::u8path(real), obui::builtInExpectedElf());
		std::printf("real disc: verdict %d steps %d serial %s crc %08X: %s\n", static_cast<int>(r.verdict), r.stepsPassed,
			r.serial.c_str(), r.crc, r.message.c_str());
		Check(r.verdict == obui::DiscVerdict::Ok, "disc: the real disc (OB_UI_REAL_DISC) verified");
	}

	const obui::ExpectedElf built = obui::builtInExpectedElf();
	Check(built.serial == "SLPM-65428" && built.size == 1294104u && built.crc == 0x32088394u &&
		built.sha256 == "975dd77d1e1c81f0d8f5f072b35c487c5ad6befbb99b339641a4c9573a334480", "disc: built-in expected ELF");
}

// ---- menu trigger ----
static uint16_t Pressed(uint16_t bits) { return static_cast<uint16_t>(~bits); }

static void TriggerTests()
{
	using obui::kPadSelect;
	using obui::kPadStart;
	constexpr uint16_t kCross = 1u << 14;
	{
		// The menu's setting since 2026-10-26: no hold-back and no combo. Start and Select pass on their first read,
		// alone or together; holding Back + Start never toggles; F1 still does.
		obui::MenuTrigger t{obui::MenuTrigger::Config{1.0, 0.0, false, false}};
		Check((t.filterPad(Pressed(kPadStart), 0.0) & kPadStart) == 0, "trigger (no hold-back): Start shows pressed on the first read");
		t.filterPad(0xFFFFu, 0.05);
		const uint16_t both = t.filterPad(Pressed(kPadSelect | kPadStart), 0.1);
		Check((both & kPadSelect) == 0 && (both & kPadStart) == 0, "trigger (no hold-back): Select + Start together both pass");
		Check(!t.update(0.1, true, true, false, false, false) && !t.update(0.6, true, true, false, false, false) &&
			!t.update(1.15, true, true, false, false, false) && !t.update(2.5, true, true, false, false, false),
			"trigger (no combo): holding Back + Start never toggles");
		Check(t.update(2.6, false, false, false, true, false), "trigger (no combo): F1 still toggles");
	}
	{
		// Windows' menu (2026-10-26): the combo held 5 s, no hold-back; comboProgress drives the gear ring.
		obui::MenuTrigger t{obui::MenuTrigger::Config{5.0, 0.0, false, true}};
		Check((t.filterPad(Pressed(kPadSelect | kPadStart), 0.0) & (kPadSelect | kPadStart)) == 0, "trigger (5 s combo): Select + Start pass at once");
		Check(!t.update(0.0, true, true, false, false, false) && t.comboProgress(0.0) == 0.0f, "trigger (5 s combo): progress starts at 0");
		Check(!t.update(2.5, true, true, false, false, false) && t.comboProgress(2.5) > 0.49f && t.comboProgress(2.5) < 0.51f,
			"trigger (5 s combo): half way at 2.5 s, no toggle");
		Check(!t.update(3.0, true, false, false, false, false) && t.comboProgress(3.0) == 0.0f, "trigger (5 s combo): letting go resets the ring");
		t.update(3.1, true, true, false, false, false);
		Check(!t.update(8.0, true, true, false, false, false) && t.update(8.2, true, true, false, false, false),
			"trigger (5 s combo): toggles 5 s after both are held again");
		Check(t.comboProgress(8.3) == 0.0f, "trigger (5 s combo): ring hidden once fired");
	}
	{
		obui::MenuTrigger t;
		Check(t.update(0.0, false, false, false, true, false), "trigger: F1 toggles");
		Check(t.update(0.1, false, false, false, false, true), "trigger: Esc toggles");
		Check(!t.update(0.2, false, false, false, false, false), "trigger: nothing pressed, no toggle");
		Check(!t.update(0.3, false, false, true, false, false), "trigger: Guide off by default");
	}
	{
		obui::MenuTrigger t(obui::MenuTrigger::Config{1.0, 0.1, true});
		Check(t.update(0.0, false, false, true, false, false) && !t.update(0.1, false, false, true, false, false),
			"trigger: Guide when enabled, once per press");
	}
	{
		obui::MenuTrigger t;
		bool fired = false;
		double firedAt = -1;
		for (double now = 0.0; now < 1.5; now += 1.0 / 60.0)
			if (t.update(now, true, true, false, false, false) && !fired)
			{
				fired = true;
				firedAt = now;
			}
		Check(fired && firedAt >= 0.99 && firedAt < 1.05, "trigger: Back+Start combo fires after 1 s");
		Check(!t.update(1.6, true, true, false, false, false), "trigger: combo fires once per hold");
		t.update(1.7, false, false, false, false, false);
		bool again = false;
		for (double now = 1.8; now < 2.9; now += 1.0 / 60.0)
			again |= t.update(now, true, true, false, false, false);
		Check(again, "trigger: combo fires again after both are released");
	}
	{
		// A lone Start: hidden for the hold-back window, then the game sees it.
		obui::MenuTrigger t;
		Check(t.filterPad(0xFFFF, 0.0) == 0xFFFF, "pad: idle passes");
		Check((t.filterPad(Pressed(kPadStart), 0.0) & kPadStart) != 0, "pad: Start hidden at first");
		Check((t.filterPad(Pressed(kPadStart), 0.05) & kPadStart) != 0, "pad: Start hidden inside the window");
		Check((t.filterPad(Pressed(kPadStart), 0.12) & kPadStart) == 0, "pad: Start reaches the game after the window");
		Check((t.filterPad(Pressed(kPadStart), 0.5) & kPadStart) == 0, "pad: Start stays pressed while held");
		Check((t.filterPad(0xFFFF, 0.6) & kPadStart) != 0, "pad: Start released");
		Check((t.filterPad(Pressed(kCross), 0.7) & kCross) == 0, "pad: other buttons never held back");
	}
	{
		// Combo: both hidden until both are released.
		obui::MenuTrigger t;
		t.filterPad(Pressed(kPadSelect), 0.0);
		uint16_t out = t.filterPad(Pressed(kPadSelect | kPadStart), 0.05);
		Check((out & (kPadSelect | kPadStart)) == (kPadSelect | kPadStart), "pad: combo hides both");
		out = t.filterPad(Pressed(kPadSelect | kPadStart), 0.9);
		Check((out & (kPadSelect | kPadStart)) == (kPadSelect | kPadStart), "pad: combo still hidden after the window");
		out = t.filterPad(Pressed(kPadStart), 1.2);
		Check((out & kPadStart) != 0, "pad: one let go, the other still hidden");
		out = t.filterPad(0xFFFF, 1.3);
		Check(out == 0xFFFF, "pad: no late tap after a combo");
	}
	{
		// A quick tap: delivered late, for a few reads.
		obui::MenuTrigger t;
		t.filterPad(Pressed(kPadStart), 0.0);
		const uint16_t first = t.filterPad(0xFFFF, 0.04);
		const uint16_t second = t.filterPad(0xFFFF, 0.06);
		int shown = 0;
		for (int i = 0; i < 10; ++i)
			shown += (t.filterPad(0xFFFF, 0.1 + i * 0.016) & kPadStart) == 0;
		Check((first & kPadStart) == 0 && (second & kPadStart) == 0 && shown == 2, "pad: a tap inside the window arrives late (4 reads)");
	}
	{
		// Gate: everything released while open; after closing, held buttons stay masked until let go.
		obui::MenuTrigger t;
		t.setMenuOpen(true, 0.0);
		Check(t.menuOpen() && t.filterPad(Pressed(kCross), 0.1) == 0xFFFF, "gate: menu open, game sees nothing");
		t.setMenuOpen(false, 0.2);
		Check((t.filterPad(Pressed(kCross), 0.3) & kCross) != 0, "gate: button held through closing stays masked");
		Check((t.filterPad(Pressed(kCross), 0.4) & kCross) != 0, "gate: masked until released");
		t.filterPad(0xFFFF, 0.5);
		Check((t.filterPad(Pressed(kCross), 0.6) & kCross) == 0, "gate: a new press after release reaches the game");
	}
	Check(obui::reservedKeyNames().size() == 2, "trigger: F1 and Esc reserved");
}

// ---- memory cards ----
static constexpr uint32_t kRaw = 528, kPages = 16384;

static void PutPageLe32(std::vector<uint8_t>& c, uint32_t page, uint32_t off, uint32_t v) { PutLe32(c, size_t(page) * kRaw + off, v); }

static void PutEntry(std::vector<uint8_t>& c, uint32_t page, uint16_t mode, uint32_t length, const char* name)
{
	uint8_t* e = &c[size_t(page) * kRaw];
	std::memset(e, 0, 512);
	e[0] = static_cast<uint8_t>(mode);
	e[1] = static_cast<uint8_t>(mode >> 8);
	PutLe32(c, size_t(page) * kRaw + 4, length);
	std::memcpy(e + 0x40, name, std::strlen(name));
}

// A formatted card as mcd.py format makes it (pages/cluster 2, indirect FAT at 8, FAT at 9, data from cluster 41),
// root directory "." "..", plus one save directory in the root's second cluster.
static std::vector<uint8_t> MakeFormattedCard()
{
	std::vector<uint8_t> c(size_t(kPages) * kRaw, 0);
	std::memcpy(c.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35);
	c[0x2A] = 2;                       // pages per cluster
	PutPageLe32(c, 0, 0x34, 41);       // alloc_offset
	PutPageLe32(c, 0, 0x3C, 0);        // root directory cluster
	PutPageLe32(c, 0, 0x50, 8);        // first indirect FAT cluster
	PutPageLe32(c, 8 * 2, 0, 9);       // indirect FAT -> FAT cluster 9
	PutPageLe32(c, 9 * 2, 0, 0x80000001u); // FAT: root cluster 0 -> 1 (in use)
	PutPageLe32(c, 9 * 2, 4, 0xFFFFFFFFu); // cluster 1: end of chain
	PutEntry(c, (41 + 0) * 2 + 0, 0x8427, 3, ".");
	PutEntry(c, (41 + 0) * 2 + 1, 0xA426, 0, "..");
	PutEntry(c, (41 + 1) * 2 + 0, 0x8427, 2, "BISLPM-65428NETBIO");
	return c;
}

// Writing cards (formatCardIfBlank, addCardFolder, readCardFile): the BWNETCNF network settings folder the game needs
// before NET CONNECTION (reports/2026-10-26-online-play.md entry 57), ported from tools/ob/mcd.py format / netcnf.
static std::vector<uint8_t> ReadAllBytes(const fs::path& p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void CardWriteTests()
{
	const fs::path dir = TempDir("cardwrite");
	const fs::path card = dir / "Mcd001.ps2";
	std::string err;
	Check(obui::formatCardIfBlank(card, &err) && err.empty(), "card write: a missing card is formatted");
	obui::CardInfo info = obui::readCard(card);
	Check(info.formatted && info.sizeBytes == 8650752u && info.saves.empty() && info.error.empty(), "card write: formatted, empty");
	const std::vector<uint8_t> fresh = ReadAllBytes(card);
	// An all-zero data page carries the ECC of zeros (77 7f 7f per 128 bytes) and four 0xFF (mcd.py raw_page).
	const std::vector<uint8_t> zeroSpare = {0x77, 0x7F, 0x7F, 0x77, 0x7F, 0x7F, 0x77, 0x7F, 0x7F, 0x77, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF};
	Check(fresh.size() == 8650752u && std::equal(zeroSpare.begin(), zeroSpare.end(), fresh.begin() + 100 * 528 + 512),
		"card write: page ECC as mcd.py");
	Check(std::memcmp(fresh.data() + 0x1C, "1.2.0.0", 7) == 0, "card write: superblock version");

	WriteBytes(card, std::vector<uint8_t>(8650752u, 0xFF));
	Check(obui::formatCardIfBlank(card, &err) && obui::readCard(card).formatted, "card write: an all-0xFF card is formatted");

	const fs::path game = dir / "game.ps2";
	WriteBytes(game, MakeFormattedCard());
	const std::vector<uint8_t> before = ReadAllBytes(game);
	Check(obui::formatCardIfBlank(game, &err) && ReadAllBytes(game) == before, "card write: a formatted card is left alone");
	const fs::path junk = dir / "junk.ps2";
	WriteBytes(junk, std::vector<uint8_t>(8650752u, 0x5A));
	err.clear();
	Check(!obui::formatCardIfBlank(junk, &err) && !err.empty() && ReadAllBytes(junk) == std::vector<uint8_t>(8650752u, 0x5A),
		"card write: a card with unknown data is never formatted");

	// A folder of files, one spanning many clusters (the network icon, 34,840 bytes = 35 clusters of 1 KiB).
	std::vector<uint8_t> icon(34840);
	for (size_t i = 0; i < icon.size(); i++)
		icon[i] = uint8_t(i * 7 + 3);
	const std::vector<obui::CardFile> files = {{"SYS_NET.ICO", icon}, {"icon.sys", std::vector<uint8_t>(964, 0x11)},
		{"BWNETCNF", {'0', ',', '1', '\n'}}, {"empty", {}}};
	Check(obui::addCardFolder(card, "BWNETCNF", files, &err), "card write: folder added");
	info = obui::readCard(card);
	Check(info.saves.size() == 1 && info.saves[0] == "BWNETCNF", "card write: the root lists the folder");
	std::vector<uint8_t> back;
	bool same = true;
	for (const obui::CardFile& f : files)
		same = same && obui::readCardFile(card, "BWNETCNF", f.name, back) && back == f.data;
	Check(same, "card write: every file reads back as written");
	Check(!obui::readCardFile(card, "BWNETCNF", "missing", back), "card write: a missing file is not found");

	const std::vector<uint8_t> withFolder = ReadAllBytes(card);
	err.clear();
	Check(!obui::addCardFolder(card, "BWNETCNF", files, &err) && !err.empty() && ReadAllBytes(card) == withFolder,
		"card write: an existing folder is never overwritten");
	Check(obui::addCardFolder(card, "SECOND", {{"a", std::vector<uint8_t>(3000, 0x22)}}, &err), "card write: a second folder");
	info = obui::readCard(card);
	Check(info.saves.size() == 2 && obui::readCardFile(card, "BWNETCNF", "SYS_NET.ICO", back) && back == icon &&
		obui::readCardFile(card, "SECOND", "a", back) && back == std::vector<uint8_t>(3000, 0x22),
		"card write: both folders intact");
	err.clear();
	Check(!obui::addCardFolder(junk, "X", files, &err) && !err.empty(), "card write: no folder on an unformatted card");
}

// The network settings folder at game start (ensureNetworkFolder): a blank card is formatted, BWNETCNF added once; an
// existing BWNETCNF and the other saves are left as they are.
static void EnsureNetworkFolderTests()
{
	const fs::path dir = TempDir("netfolder");
	const fs::path card = dir / "Mcd001.ps2";
	const std::vector<obui::CardFile> files = {{"SYS_NET.ICO", std::vector<uint8_t>(0x8398, 1)}, {"icon.sys", std::vector<uint8_t>(0x3C4, 2)},
		{"BWNETCNF", {'0'}}, {"net000.cnf", {'n'}}, {"ifc000.dat", {'i'}}, {"dev000.dat", {'d'}}};
	std::string err;
	Check(obui::ensureNetworkFolder(card, files, &err) == obui::NetworkFolder::FormattedAndAdded, "net folder: a missing card is formatted and gets BWNETCNF");
	std::vector<uint8_t> back;
	Check(obui::readCardFile(card, "BWNETCNF", "ifc000.dat", back) && back == std::vector<uint8_t>{'i'}, "net folder: its files are on the card");
	const std::vector<uint8_t> once = ReadAllBytes(card);
	Check(obui::ensureNetworkFolder(card, files, &err) == obui::NetworkFolder::AlreadyThere && ReadAllBytes(card) == once,
		"net folder: a card with BWNETCNF is left alone");

	const fs::path game = dir / "game.ps2";
	Check(obui::formatCardIfBlank(game, &err) && obui::addCardFolder(game, "BISLPM-65428NETBIO", {{"save", std::vector<uint8_t>(5000, 9)}}, &err),
		"net folder: a card with the game's save");
	Check(obui::ensureNetworkFolder(game, files, &err) == obui::NetworkFolder::Added, "net folder: BWNETCNF added beside the save");
	const obui::CardInfo info = obui::readCard(game);
	Check(info.saves.size() == 2 && obui::readCardFile(game, "BISLPM-65428NETBIO", "save", back) && back == std::vector<uint8_t>(5000, 9),
		"net folder: the game's save is intact");

	const fs::path junk = dir / "junk.ps2";
	WriteBytes(junk, std::vector<uint8_t>(8650752u, 0x5A));
	err.clear();
	Check(obui::ensureNetworkFolder(junk, files, &err) == obui::NetworkFolder::Failed && !err.empty(), "net folder: an unknown card fails, untouched");

	// Security review C1: a formatted card whose layout fields or FAT point outside the card is refused untouched
	// (they indexed memory unchecked: a downloaded or corrupt card could write outside the buffer).
	const std::vector<uint8_t> good = ReadAllBytes(card);
	auto put32 = [](std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
	struct Corruption { const char* what; size_t at; uint32_t value; };
	const Corruption bad[] = {
		{"alloc offset past the card", 0x34, 0x00FFFFFFu},
		{"root cluster past the allocatable area", 0x3C, 0x00FFFFFFu},
		{"indirect FAT cluster past the card", 0x50, 0x00FFFFFFu},
	};
	for (const Corruption& c : bad)
	{
		std::vector<uint8_t> b = good;
		put32(b, c.at, c.value);
		const fs::path f = dir / "corrupt.ps2";
		WriteBytes(f, b);
		err.clear();
		const obui::NetworkFolder r = obui::ensureNetworkFolder(f, files, &err);
		Check((r == obui::NetworkFolder::Failed || r == obui::NetworkFolder::AlreadyThere) && ReadAllBytes(f) == b,
			(std::string("net folder: a card with its ") + c.what + " is refused, untouched").c_str());
		std::vector<uint8_t> out;
		(void)obui::readCardFile(f, "BWNETCNF", "ifc000.dat", out); // must not crash
	}
	{
		// A FAT whose first data cluster chains far past the card (0x80FFFFFF): the walk must stop, not index off.
		std::vector<uint8_t> b = ReadAllBytes(game);
		const uint32_t ifc0 = 0, ppc = 2;
		uint32_t indirect = 0, fatCluster = 0;
		std::memcpy(&indirect, &b[0x50], 4);
		std::memcpy(&fatCluster, &b[size_t(indirect) * ppc * 528], 4);
		(void)ifc0;
		put32(b, size_t(fatCluster) * ppc * 528, 0x80FFFFFFu); // FAT entry of cluster 0
		const fs::path f = dir / "badfat.ps2";
		WriteBytes(f, b);
		err.clear();
		(void)obui::ensureNetworkFolder(f, files, &err); // any result, no crash
		std::vector<uint8_t> out;
		(void)obui::readCardFile(f, "BISLPM-65428NETBIO", "save", out);
		Check(true, "net folder: a FAT chaining past the card does not crash");
	}
}

static void CardTests()
{
	const fs::path dir = TempDir("cards");
	Check(!obui::readCard(dir / "Mcd001.ps2").present, "card: missing card not present");
	WriteBytes(dir / "Mcd001.ps2", std::vector<uint8_t>(size_t(kPages) * kRaw, 0xFF));
	obui::CardInfo info = obui::readCard(dir / "Mcd001.ps2");
	Check(info.present && !info.formatted && info.sizeBytes == 8650752u && info.saves.empty(), "card: blank card is unformatted");

	WriteBytes(dir / "Mcd001.ps2", MakeFormattedCard());
	info = obui::readCard(dir / "Mcd001.ps2");
	Check(info.formatted && info.error.empty() && info.saves.size() == 1 && info.saves[0] == "BISLPM-65428NETBIO",
		"card: formatted card lists its save");

	// Transfer: Copy keeps the source, never overwrites a card already at the destination; Move removes the source.
	const fs::path to = dir / "new";
	std::string err;
	Check(obui::transferCards(dir, to, obui::CardMove::Copy, &err) && fs::exists(to / "Mcd001.ps2") && fs::exists(dir / "Mcd001.ps2"),
		"card: copy to a new folder");
	WriteBytes(dir / "Mcd001.ps2", std::vector<uint8_t>(16, 1));
	err.clear();
	Check(!obui::transferCards(dir, to, obui::CardMove::Move, &err) && !err.empty() && obui::readCard(to / "Mcd001.ps2").formatted &&
		fs::exists(dir / "Mcd001.ps2"), "card: an existing destination card is never overwritten");
	const fs::path to2 = dir / "moved";
	Check(obui::transferCards(to, to2, obui::CardMove::Move, &err) && fs::exists(to2 / "Mcd001.ps2") && !fs::exists(to / "Mcd001.ps2"),
		"card: move");
	Check(obui::transferCards(dir, dir / "x", obui::CardMove::Leave, &err) && !fs::exists(dir / "x" / "Mcd001.ps2"), "card: leave");

	// Import replaces slot 1; a non-card is refused.
	Check(obui::importCard(to2 / "Mcd001.ps2", dir, &err) && obui::readCard(dir / "Mcd001.ps2").formatted, "card: import from PCSX2");
	WriteBytes(dir / "notacard.bin", std::vector<uint8_t>(1000, 0));
	Check(!obui::importCard(dir / "notacard.bin", dir, &err), "card: import refuses a non-card");

	// Backups keep the newest N (seed older ones by name).
	fs::create_directories(dir / "backups");
	for (int i = 0; i < 5; ++i)
		WriteBytes(dir / "backups" / ("Mcd001-2000010" + std::to_string(i) + "-000000.ps2.bak"), {1});
	Check(obui::backupCard(dir / "Mcd001.ps2", 3, &err), "card: backup");
	int n = 0;
	bool oldestGone = true;
	for (const auto& e : fs::directory_iterator(dir / "backups"))
	{
		++n;
		if (e.path().filename().string().find("20000100") != std::string::npos)
			oldestGone = false;
	}
	Check(n == 3 && oldestGone, "card: backups keep the newest 3");
}

static void WriteText(const fs::path& p, const std::string& t)
{
	std::ofstream o(p, std::ios::binary);
	o << t;
}

static void WidescreenTests()
{
	// Detection: the patch line or gsaspectratio=16:9, case and spaces ignored; comment lines do not count.
	Check(obui::isWidescreenPnach("gametitle=x\r\n[Widescreen 16:9]\r\ngsaspectratio=16:9\r\n"), "widescreen: gsaspectratio line");
	Check(obui::isWidescreenPnach("PATCH = 1, EE, 20239D48, extended, 3FE38E38 // gameplay\n"), "widescreen: patch line, any case and spaces");
	Check(!obui::isWidescreenPnach("//patch=1,EE,20239d48,extended,3fe38e38\n; gsaspectratio=16:9\n"), "widescreen: comment lines ignored");
	Check(!obui::isWidescreenPnach("patch=1,EE,00100000,extended,00000000\ngsaspectratio=4:3\n"), "widescreen: another patch is not");

	const fs::path dir = TempDir("widescreen");
	const std::string ws = "gametitle=t\n[Widescreen 16:9]\npatch=1,EE,20239d48,extended,3fe38e38\n";
	WriteText(dir / "other.pnach", "patch=1,EE,00100000,extended,00000000\n");
	std::string err;

	// None: 16:9 writes ob-widescreen.pnach; the other mod is not one.
	Check(!obui::widescreenEnabled(dir), "widescreen: off with no widescreen mod");
	Check(obui::setWidescreen(dir, true, &err) && fs::exists(dir / obui::kOwnWidescreenFile), "widescreen: 16:9 creates ob-widescreen.pnach when none");
	Check(obui::widescreenEnabled(dir) && obui::findWidescreenMods(dir).size() == 1, "widescreen: on, one mod (other.pnach not counted)");

	// 4:3 disables every enabled one by rename (the player's file and ours), nothing deleted.
	WriteText(dir / "widescreen.pnach", ws);
	Check(obui::setWidescreen(dir, false, &err), "widescreen: 4:3");
	Check(fs::exists(dir / "widescreen.pnach.disabled") && fs::exists(dir / "ob-widescreen.pnach.disabled") &&
			  !fs::exists(dir / "widescreen.pnach") && fs::exists(dir / "other.pnach"),
		"widescreen: 4:3 renames all widescreen mods to .disabled, keeps the rest");
	Check(!obui::widescreenEnabled(dir), "widescreen: off after 4:3");

	// 16:9 again: enables one existing disabled mod, the player's own before ours; writes nothing new.
	Check(obui::setWidescreen(dir, true, &err), "widescreen: 16:9 again");
	Check(fs::exists(dir / "widescreen.pnach") && fs::exists(dir / "ob-widescreen.pnach.disabled") &&
			  !fs::exists(dir / "ob-widescreen.pnach"),
		"widescreen: 16:9 enables the player's disabled file, only one");
	Check(obui::isWidescreenModFile(dir / "ob-widescreen.pnach.disabled") && !obui::isWidescreenModFile(dir / "other.pnach"),
		"widescreen: file test by name or content");
}

static void TouchTests()
{
	obui::TouchPad t;
	uint8_t sticks[4] = {0x80, 0x80, 0x80, 0x80};
	Check(t.merge(0xFFFFu, sticks) == 0xFFFFu && t.physicalReads() == 0, "touch: idle passes, no physical use");

	// Cross (0x4000) held by touch, Circle (0x2000) by the controller: both reach the game.
	const uint8_t centre[4] = {0x80, 0x80, 0x80, 0x80};
	t.set(0x4000u, centre);
	const uint16_t both = t.merge(static_cast<uint16_t>(~0x2000u), sticks);
	Check((both & 0x6000u) == 0 && (both | 0x6000u) == 0xFFFFu, "touch: touch and pad buttons both down, nothing else");
	Check(t.physicalReads() == 1, "touch: a held pad button counts as physical use");

	// Touch alone is not physical use.
	t.merge(0xFFFFu, sticks);
	Check(t.physicalReads() == 1, "touch: touch alone is not physical use");

	// A pushed touch left stick axis (byte 2) replaces the pad's; a centred touch axis leaves the pad's.
	const uint8_t left[4] = {0x80, 0x80, 0xFF, 0x80};
	t.set(0, left);
	uint8_t s2[4] = {0x10, 0x80, 0x80, 0x90};
	t.merge(0xFFFFu, s2);
	Check(s2[2] == 0xFF && s2[3] == 0x90, "touch: pushed axis wins, centred axis keeps the pad's");
	Check(s2[0] == 0x10, "touch: the pad's right stick passes while touch leaves it centred");
	Check(t.physicalReads() == 2, "touch: a pad stick past the threshold counts as physical use");

	// A small touch offset (within kTouchStick) is centre; a small pad drift is not use.
	const uint8_t nudge[4] = {0x80, 0x80, 0x85, 0x80};
	t.set(0, nudge);
	uint8_t s3[4] = {0x80, 0x80, 0x90, 0x80};
	t.merge(0xFFFFu, s3);
	Check(s3[2] == 0x90 && t.physicalReads() == 2, "touch: small touch offset ignored, small pad drift not counted");

	// clear() lets everything go.
	t.set(0xFFFFu, left);
	t.clear();
	uint8_t s4[4] = {0x80, 0x80, 0x80, 0x80};
	Check(t.merge(0xFFFFu, s4) == 0xFFFFu && s4[2] == 0x80, "touch: clear releases buttons and sticks");
}

static void ScreenTests()
{
	// A synthetic EE RAM: the game task in its slot, the menu byte, the controller type (game_screen.h).
	std::vector<uint8_t> ram(32u * 1024u * 1024u, 0u);
	auto put32 = [&](uint32_t a, uint32_t v) {
		for (int i = 0; i < 4; i++)
			ram[a + i] = static_cast<uint8_t>(v >> (8 * i));
	};
	Check(!obui::classifyScreen(nullptr, 0).known, "screen: no RAM = unknown");
	Check(!obui::classifyScreen(ram.data(), 0x1000).known, "screen: RAM too small = unknown");

	obui::GameScreen g = obui::classifyScreen(ram.data(), static_cast<uint32_t>(ram.size()));
	Check(g.known && g.menu, "screen: before the game task (title, select) = menu layout");

	put32(obui::kGameTaskSlot, obui::kGameTaskFunc);
	g = obui::classifyScreen(ram.data(), static_cast<uint32_t>(ram.size()));
	Check(g.known && !g.menu, "screen: in the game task, no menu open = play layout");

	ram[obui::kMenuOpen] = 1;
	ram[obui::kControlType] = 2;
	g = obui::classifyScreen(ram.data(), static_cast<uint32_t>(ram.size()));
	Check(g.menu && g.controlType == 2, "screen: status / map / pause open = menu layout, with the controller type");

	// The attached view follows the game's writes.
	obui::attachGuestRam(ram.data(), static_cast<uint32_t>(ram.size()));
	ram[obui::kMenuOpen] = 0;
	Check(obui::currentScreen().known && !obui::currentScreen().menu, "screen: the attached RAM is read live");
	obui::attachGuestRam(nullptr, 0);
	Check(!obui::currentScreen().known, "screen: detached = unknown");
}

// The Network page's rules (core/net_choice.h; the Android launcher's NetPlay.kt has the same cases): strict addresses,
// the game's settings from the saved choices, nothing that could inject a server name or remap.
static void NetTests()
{
	using obui::NetConnection;
	using Env = std::vector<std::pair<std::string, std::string>>;
	Check(obui::isIpv4("192.168.0.76") && obui::isIpv4("0.0.0.0"), "net: dotted quads");
	for (const char* bad : {"", "192.168.0", "192.168.0.256", "1.2.3.4.5", "1..2.3", "a.b.c.d", " 1.2.3.4", "1.2.3.4;",
			 "0192.168.0.1", "1.2.3.4=5.6.7.8", "::1"})
		Check(!obui::isIpv4(bad), (std::string("net: not an address: '") + bad + "'").c_str());
	Check(obui::isLocalIpv4("10.0.0.2") && obui::isLocalIpv4("172.31.255.254") && obui::isLocalIpv4("192.168.1.20") &&
			  obui::isLocalIpv4("169.254.3.4"),
		"net: private and link-local are local");
	Check(!obui::isLocalIpv4("172.32.0.1") && !obui::isLocalIpv4("8.8.8.8") && !obui::isLocalIpv4("127.0.0.1") &&
			  !obui::isLocalIpv4("255.255.255.255"),
		"net: public, loopback and broadcast are not");
	Check(obui::broadcastAddress("192.168.0.76", 24) == "192.168.0.255" &&
			  obui::broadcastAddress("172.16.2.9", 23) == "172.16.3.255" && obui::broadcastAddress("1.2.3.4", 0).empty() &&
			  obui::broadcastAddress("x", 24).empty(),
		"net: broadcast address from the prefix");

	const NetConnection dhcp;
	Check(obui::netcnfValue(dhcp) == "dhcp", "net: DHCP");
	NetConnection manual{false, "192.168.0.80", "255.255.255.0", "192.168.0.1", "192.168.0.1", ""};
	Check(obui::netcnfValue(manual) == "static:192.168.0.80,255.255.255.0,192.168.0.1,192.168.0.1", "net: manual");
	manual.dns2 = "8.8.8.8";
	Check(obui::netcnfValue(manual) == "static:192.168.0.80,255.255.255.0,192.168.0.1,192.168.0.1,8.8.8.8", "net: manual, DNS 2");
	manual.address = "192.168.0.80,1.2.3.4";
	Check(obui::netcnfValue(manual).empty(), "net: a manual field that is not one address is refused");

	const Env join = {{"PS2X_DEV9", "pcsx2"}, {"PS2X_NET_IF", "wlan0"},
		{"PS2X_NET_HOSTS", "www01.kddi-mmbb.jp=192.168.0.50;gate1.jp.dnas.playstation.org=192.168.0.50"},
		{"PS2X_NET_REMAP", "192.168.0.50:443=192.168.0.50:18443"}, {"PS2X_NETCNF", "dhcp"}};
	Check(obui::gameNetEnv("192.168.0.50", 18443, "wlan0", dhcp) == join, "net: a host on a high port gets the login remapped");
	const Env server = obui::gameNetEnv("203.0.113.9", 443, "", dhcp);
	Check(server.size() == 3 && server[1].second == "www01.kddi-mmbb.jp=203.0.113.9;gate1.jp.dnas.playstation.org=203.0.113.9",
		"net: a server on 443, automatic interface: no remap, no interface");
	Check(obui::gameNetEnv("192.168.0.50;evil=1.2.3.4", 443, "", dhcp).empty() &&
			  obui::gameNetEnv("example.com", 443, "", dhcp).empty() && obui::gameNetEnv("192.168.0.50", 0, "", dhcp).empty() &&
			  obui::gameNetEnv("192.168.0.50", 65536, "", dhcp).empty() &&
			  obui::gameNetEnv("192.168.0.50", 443, "wlan0;x", dhcp).empty(),
		"net: anything that could inject a setting is refused");

	auto ui = [](std::vector<std::pair<std::string, std::string>> kv) {
		return [kv](const std::string& key) {
			for (const auto& [k, v] : kv)
				if (k == key)
					return v;
			return std::string();
		};
	};
	Check(obui::chosenNetEnv(ui({}), "").empty() && obui::chosenNetEnv(ui({{"net_mode", "off"}, {"net_server", "203.0.113.9"}}), "").empty(),
		"net: off or unset = offline");
	Check(obui::chosenNetEnv(ui({{"net_mode", "local"}, {"net_join", "192.168.0.50:18443"}}), "wlan0") == join,
		"net: local play joins the chosen host");
	Check(obui::chosenNetEnv(ui({{"net_mode", "local"}, {"net_join", "203.0.113.9:443"}}), "").empty() &&
			  obui::chosenNetEnv(ui({{"net_mode", "local"}, {"net_join", "192.168.0.50"}}), "").empty(),
		"net: local play joins local addresses with a port only");
	const Env fanDns = {{"PS2X_DEV9", "pcsx2"}, {"PS2X_NET_DNS", "208.72.237.31"}, {"PS2X_NETCNF", "dhcp"}};
	Check(obui::chosenNetEnv(ui({{"net_mode", "server"}, {"net_server", "208.72.237.31"}}), "") == fanDns,
		"net: a fan server is the game's DNS server (report entry 65)");
	const obui::EnvList fanManual = obui::chosenNetEnv(ui({{"net_mode", "server"}, {"net_server", "208.72.237.31"},
		{"net_connection", "manual"}, {"net_address", "192.168.0.80"}, {"net_netmask", "255.255.255.0"},
		{"net_gateway", "192.168.0.1"}, {"net_dns1", "192.168.0.1"}}), "");
	Check(!fanManual.empty() && fanManual.back().second == "static:192.168.0.80,255.255.255.0,192.168.0.1,208.72.237.31,192.168.0.1",
		"net: manual settings with a fan server put its DNS first");
	Check(obui::chosenNetEnv(ui({{"net_mode", "server"}, {"net_server", "203.0.113.9"}, {"net_connection", "manual"},
								  {"net_address", "192.168.0.80"}}),
			  "")
			  .empty(),
		"net: incomplete manual settings = offline");
	// Security queue 2, F1: a fan server's DNS must be an address a server can have.
	for (const char* bad : {"127.0.0.1", "0.0.0.0", "0.1.2.3", "224.0.0.1", "239.1.1.1", "255.255.255.255"})
		Check(obui::chosenNetEnv(ui({{"net_mode", "server"}, {"net_server", bad}}), "").empty(),
			(std::string("net: not a fan DNS: ") + bad).c_str());
	Check(!obui::chosenNetEnv(ui({{"net_mode", "server"}, {"net_server", "192.168.0.5"}}), "").empty(),
		"net: a fan DNS on the local network is fine");
	Check(obui::cleanHostName("  Johnny's PC  ") == "Johnny's PC" && obui::cleanHostName("\x01\x07") == "Host" &&
			  obui::cleanHostName(std::string(40, 'x')).size() == 24,
		"net: host names are short and printable");
}

// Security queue C5: text from files (card save names, ini values) handed to Android's NewStringUTF must be valid
// Modified UTF-8, or a debuggable build aborts. Invalid bytes, NUL and 4-byte characters become '?'.
static void SafeTextTests()
{
	// The saved Output resolution, applied to the game window at Play.
	{
		int w = 0, h = 0;
		Check(obui::parseWindowSize("1920x1080", w, h) && w == 1920 && h == 1080, "window size: 1920x1080");
		for (const char* bad : {"", "1920", "x1080", "1920x", "axb", "0x0", "1920x1080x3", "-1x5", "99999x99999"})
			Check(!obui::parseWindowSize(bad, w, h), (std::string("window size refused: '") + bad + "'").c_str());
	}
	// Security queue 2, F3: a value with a line break must not become a second setting.
	{
		obui::IniFile injected;
		injected.parse("a = 1\n");
		injected.set("", "a", "2\nnet_mode = server\r\nb = 3");
		Check(injected.text().find("\nnet_mode") == std::string::npos && injected.text().find("\nb =") == std::string::npos,
			"ini: a line break in a value does not add a line");
	}
	// F4: a card file far larger than a card is refused without being read into memory.
	{
		const fs::path big = TempDir("bigcard") / "big.ps2";
		{
			std::ofstream f(big, std::ios::binary);
			f.seekp(64 * 1024 * 1024);
			f.put('x');
		}
		const obui::CardInfo info = obui::readCard(big);
		Check(info.present && !info.formatted, "card: an oversized file is not read as a card");
	}
	Check(obui::javaSafeText("Mcd001 OK") == "Mcd001 OK", "safe text: ASCII unchanged");
	Check(obui::javaSafeText("caf\xC3\xA9 \xE3\x81\x82") == "caf\xC3\xA9 \xE3\x81\x82", "safe text: valid 2- and 3-byte UTF-8 unchanged");
	Check(obui::javaSafeText("a\x80" "b") == "a?b", "safe text: a lone continuation byte");
	Check(obui::javaSafeText("a\xC3") == "a?", "safe text: a truncated sequence at the end");
	Check(obui::javaSafeText("a\xC0\xAF" "b") == "a??b", "safe text: an overlong form");
	Check(obui::javaSafeText("\xFF\xFE") == "??", "safe text: bytes never valid in UTF-8");
	Check(obui::javaSafeText("x\xF0\x9F\x98\x80y") == "x?y", "safe text: a 4-byte character (emoji)");
	Check(obui::javaSafeText(std::string("a\0b", 3)) == "a?b", "safe text: a NUL byte");
	Check(obui::javaSafeText("\xED\xA0\x80") == "???", "safe text: an encoded surrogate");
}

int main()
{
	IniTests();
	ScreenTests();
	DiscTests();
	TriggerTests();
	CardTests();
	CardWriteTests();
	EnsureNetworkFolderTests();
	WidescreenTests();
	TouchTests();
	NetTests();
	SafeTextTests();
	std::printf("ob_ui: %d/%d\n", s_total - s_fail, s_total);
	return s_fail ? 1 : 0;
}
