// ob_gpu_driver_tests: the pure part of the Android GPU driver choice (adapter/gpu_driver.h) on the host: the
// gpu_driver setting, a driver package's meta.json, which library loads, the fall-back decision and its reasons, the
// log lines, the folder sync stamps. The device part (libadrenotools, Vulkan probe) is checked on the device.
// Output as ps2x_tests ("[Passed]"/"[Failed]" lines, "Passed: N", "Total Tests: N") for ob.sh test.
#include "gpu_driver.h"

#include <cstdio>
#include <string>

namespace
{
	int g_total = 0, g_passed = 0;
	void Check(const char* name, bool ok, const std::string& detail = {})
	{
		g_total++;
		if (ok)
			g_passed++;
		std::printf("[%s] %s %s\n", ok ? "Passed" : "Failed", name, ok ? "" : detail.c_str());
	}

	// A K11MCH1/AdrenoToolsDrivers-style Turnip package's meta.json (shape from libadrenotools tools/ADPKG.md).
	const char* kTurnipMeta = R"({
  "schemaVersion": 1,
  "name": "Turnip",
  "description": "Mesa Turnip, \"test\" build\nline 2 é",
  "author": "someone",
  "packageVersion": "1",
  "vendor": "Mesa",
  "driverVersion": "25.0.0",
  "minApi": 28,
  "libraryName": "vulkan.ad07XX.so",
  "extra": {"nested": [1, 2, {"x": null}], "flag": true}
})";

	obgpu::CustomDriverCheck GoodCheck()
	{
		obgpu::CustomDriverCheck c;
		c.folder_found = c.meta_found = true;
		std::string e;
		c.meta_ok = obgpu::ParseDriverMeta(kTurnipMeta, c.meta, e);
		c.files = {"meta.json", "vulkan.ad07XX.so"};
		c.device_api = 35;
		c.kgsl_present = true;
		c.hooks_present = c.native_lib_dir_ok = true;
		return c;
	}

	void TestSetting()
	{
		obgpu::DriverChoice c = obgpu::ParseGpuDriverSetting("system");
		Check("setting: system", c.system && c.error.empty());
		c = obgpu::ParseGpuDriverSetting("");
		Check("setting: empty = system", c.system && c.error.empty());
		c = obgpu::ParseGpuDriverSetting("  SyStEm ");
		Check("setting: case and blanks", c.system && c.error.empty());
		c = obgpu::ParseGpuDriverSetting("turnip-25.0_r1");
		Check("setting: folder name", !c.system && c.name == "turnip-25.0_r1" && c.error.empty(), c.name);
		c = obgpu::ParseGpuDriverSetting("\"turnip\"");
		Check("setting: quoted", !c.system && c.name == "turnip");
		for (const char* bad : {"../etc", "a/b", "a\\b", ".hidden", "with space", "x\xC3\xA9"})
		{
			c = obgpu::ParseGpuDriverSetting(bad);
			Check((std::string("setting: rejected ") + bad).c_str(), c.system && !c.error.empty());
		}
		Check("setting: 64 chars ok, 65 rejected",
			obgpu::IsValidDriverName(std::string(64, 'a')) && !obgpu::IsValidDriverName(std::string(65, 'a')));
	}

	void TestMeta()
	{
		obgpu::DriverMeta m;
		std::string e;
		const bool ok = obgpu::ParseDriverMeta(kTurnipMeta, m, e);
		Check("meta: Turnip package parses", ok, e);
		Check("meta: fields", m.schemaVersion == 1 && m.name == "Turnip" && m.vendor == "Mesa" &&
								  m.driverVersion == "25.0.0" && m.minApi == 28 && m.libraryName == "vulkan.ad07XX.so",
			m.libraryName);
		Check("meta: escapes", m.description == "Mesa Turnip, \"test\" build\nline 2 \xC3\xA9", m.description);
		Check("meta: BOM and empty object", obgpu::ParseDriverMeta("\xEF\xBB\xBF { }", m, e), e);
		Check("meta: number versions", obgpu::ParseDriverMeta(R"({"packageVersion": 3, "driverVersion": 615})", m, e) &&
										   m.packageVersion == "3" && m.driverVersion == "615");
		for (const char* bad : {"", "[]", "{", R"({"a" 1})", R"({"a": 1,})", R"({"a": "x"} junk)", R"({"a": tru})",
				 R"({"a": "unterminated})"})
			Check((std::string("meta: rejected ") + bad).c_str(), !obgpu::ParseDriverMeta(bad, m, e) && !e.empty());
		std::string deep = "{\"a\":";
		for (int i = 0; i < 100; ++i)
			deep += "[";
		Check("meta: deep nesting rejected, no overflow", !obgpu::ParseDriverMeta(deep, m, e));
	}

	void TestLibrary()
	{
		obgpu::DriverMeta m;
		std::string e;
		m.libraryName = "vulkan.ad07XX.so";
		Check("library: libraryName", obgpu::PickDriverLibrary(m, {"meta.json", "vulkan.ad07XX.so"}, e) == "vulkan.ad07XX.so");
		Check("library: libraryName missing from folder", obgpu::PickDriverLibrary(m, {"meta.json"}, e).empty() && !e.empty());
		m.libraryName = "../../system/lib64/libvulkan.so";
		Check("library: path rejected", obgpu::PickDriverLibrary(m, {"meta.json"}, e).empty() && !e.empty());
		m.libraryName.clear();
		Check("library: one vulkan.*.so", obgpu::PickDriverLibrary(m, {"meta.json", "vulkan.turnip.so", "libfoo.so"}, e) ==
											  "vulkan.turnip.so");
		Check("library: none", obgpu::PickDriverLibrary(m, {"meta.json", "libfoo.so"}, e).empty() && !e.empty());
		Check("library: two is ambiguous",
			obgpu::PickDriverLibrary(m, {"vulkan.a.so", "vulkan.b.so"}, e).empty() && e.find("several") != std::string::npos);
	}

	void TestDecision()
	{
		std::string lib;
		obgpu::CustomDriverCheck c = GoodCheck();
		std::string r = obgpu::CustomDriverBlocker(c, lib);
		Check("decide: good package on Adreno loads", r.empty() && lib == "vulkan.ad07XX.so", r);

		c = GoodCheck();
		c.system_vendor_id = obgpu::kVendorQualcomm;
		c.kgsl_present = false;
		Check("decide: Qualcomm vendor id is Adreno", obgpu::CustomDriverBlocker(c, lib).empty());

		struct Case
		{
			const char* name;
			void (*edit)(obgpu::CustomDriverCheck&);
			const char* want;
		};
		const Case cases[] = {
			{"no folder", [](obgpu::CustomDriverCheck& c) { c.folder_found = false; }, "no folder"},
			{"no meta.json", [](obgpu::CustomDriverCheck& c) { c.meta_found = false; }, "no meta.json"},
			{"bad meta.json", [](obgpu::CustomDriverCheck& c) { c.meta_ok = false; c.meta_error = "meta.json: x"; }, "meta.json: x"},
			{"library missing", [](obgpu::CustomDriverCheck& c) { c.files = {"meta.json"}; }, "not in the folder"},
			{"Mali", [](obgpu::CustomDriverCheck& c) { c.system_vendor_id = 0x13B5; }, "not Adreno (vendor ARM)"},
			{"no kgsl, probe failed", [](obgpu::CustomDriverCheck& c) { c.kgsl_present = false; }, "not Adreno"},
			{"old Android", [](obgpu::CustomDriverCheck& c) { c.device_api = 27; }, "needs Android API 28, device has 27"},
			{"libs not extracted", [](obgpu::CustomDriverCheck& c) { c.native_lib_dir_ok = false; }, "not extracted"},
			{"hooks missing", [](obgpu::CustomDriverCheck& c) { c.hooks_present = false; }, "hooks"},
		};
		for (const Case& k : cases)
		{
			c = GoodCheck();
			k.edit(c);
			r = obgpu::CustomDriverBlocker(c, lib);
			Check((std::string("decide: falls back, ") + k.name).c_str(), r.find(k.want) != std::string::npos && lib.empty(), r);
		}
		Check("decide: fall-back line",
			obgpu::FallbackLine("turnip", "GPU is not Adreno") ==
				"[gpu] custom driver turnip unavailable (GPU is not Adreno); using system driver");
	}

	void TestLines()
	{
		obgpu::DeviceInfo d;
		d.vendor_id = 0x5143;
		d.device_name = "Adreno (TM) 830";
		d.driver_version = (512u << 22) | (819u << 12) | 2u;
		d.api_version = (1u << 22) | (3u << 12) | 284u;
		d.driver_name = "Qualcomm Technologies Inc. Adreno Vulkan Driver";
		d.driver_info = "Driver Build: x";
		const std::string sys = obgpu::DriverLine(false, "", d);
		Check("line: system", sys == "[gpu] driver=system vendor=Qualcomm(0x5143) deviceName=Adreno (TM) 830 "
									 "driverVersion=512.819.2 (0x80333002) (Qualcomm Technologies Inc. Adreno Vulkan Driver "
									 "Driver Build: x) api=1.3.284",
			sys);
		d.driver_name.clear();
		d.driver_info.clear();
		const std::string cus = obgpu::DriverLine(true, "turnip", d);
		Check("line: custom, no driver properties",
			cus.rfind("[gpu] driver=custom:turnip vendor=Qualcomm(0x5143) deviceName=", 0) == 0 &&
				cus.find(" ()") == std::string::npos,
			cus);
		Check("line: NVIDIA version bits", obgpu::FormatDriverVersion(0x10DE, (580u << 22) | (88u << 14)) == "580.88.0.0 (0x91160000)",
			obgpu::FormatDriverVersion(0x10DE, (580u << 22) | (88u << 14)));
		Check("line: unknown vendor", obgpu::VendorName(0x1234) == "0x1234");
	}

	void TestSelfRead()
	{
		using obgpu::SelfRead;
		Check("self-read setting: empty = auto", obgpu::ParseSelfReadSetting("").mode == SelfRead::Auto);
		Check("self-read setting: case and blanks", obgpu::ParseSelfReadSetting("  Copy ").mode == SelfRead::Copy);
		Check("self-read setting: barrier, fetch", obgpu::ParseSelfReadSetting("barrier").mode == SelfRead::Barrier &&
			obgpu::ParseSelfReadSetting("FETCH").mode == SelfRead::Fetch);
		const obgpu::SelfReadChoice bad = obgpu::ParseSelfReadSetting("fast");
		Check("self-read setting: rejected", bad.mode == SelfRead::Auto && !bad.error.empty(), bad.error);

		obgpu::DeviceInfo qcom; // the Odin's line: Qualcomm(0x5143) ... (Qualcomm Technologies Inc. Adreno Vulkan Driver ...)
		qcom.vendor_id = obgpu::kVendorQualcomm;
		qcom.driver_name = "Qualcomm Technologies Inc. Adreno Vulkan Driver";
		obgpu::DeviceInfo turnip = qcom;
		turnip.driver_name = "turnip Mesa driver";
		obgpu::DeviceInfo noname = qcom;
		noname.driver_name.clear();
		obgpu::DeviceInfo mali;
		mali.vendor_id = 0x13B5;
		mali.driver_name = "Mali-G715";
		Check("qualcomm proprietary: yes", obgpu::IsQualcommProprietary(qcom));
		Check("qualcomm proprietary: Turnip no", !obgpu::IsQualcommProprietary(turnip));
		Check("qualcomm proprietary: no driver name counts", obgpu::IsQualcommProprietary(noname));
		Check("qualcomm proprietary: Mali no", !obgpu::IsQualcommProprietary(mali));

		const obgpu::SelfReadDecision a = obgpu::DecideSelfRead(SelfRead::Auto, &qcom);
		Check("auto on Qualcomm = copy", a.override_texture_barriers == 0 && a.disable_framebuffer_fetch && a.road == "copy",
			obgpu::SelfReadLine(a));
		Check("auto line", obgpu::SelfReadLine(a) == "[gpu] self-read=copy (auto: Qualcomm's driver)", obgpu::SelfReadLine(a));
		const obgpu::SelfReadDecision t = obgpu::DecideSelfRead(SelfRead::Auto, &turnip);
		Check("auto on Turnip = PCSX2's choice", t.override_texture_barriers == -1 && !t.disable_framebuffer_fetch &&
			t.road == "fetch" && t.why == "auto");
		const obgpu::SelfReadDecision m = obgpu::DecideSelfRead(SelfRead::Auto, &mali);
		Check("auto on Mali = PCSX2's choice (unchanged)", m.override_texture_barriers == -1 && !m.disable_framebuffer_fetch);
		const obgpu::SelfReadDecision n = obgpu::DecideSelfRead(SelfRead::Auto, nullptr);
		Check("auto without a probe (desktop) = PCSX2's choice", n.override_texture_barriers == -1 && !n.disable_framebuffer_fetch);
		const obgpu::SelfReadDecision f = obgpu::DecideSelfRead(SelfRead::Fetch, &qcom);
		Check("fetch forced on Qualcomm", f.override_texture_barriers == -1 && !f.disable_framebuffer_fetch &&
			obgpu::SelfReadLine(f) == "[gpu] self-read=fetch (outbreak.ini)", obgpu::SelfReadLine(f));
		const obgpu::SelfReadDecision b = obgpu::DecideSelfRead(SelfRead::Barrier, nullptr);
		Check("barrier forced", b.override_texture_barriers == 1 && b.disable_framebuffer_fetch && b.road == "barrier");
		const obgpu::SelfReadDecision c = obgpu::DecideSelfRead(SelfRead::Copy, &turnip);
		Check("copy forced on Turnip", c.override_texture_barriers == 0 && c.disable_framebuffer_fetch);
	}

	void TestStamps()
	{
		const std::string a = obgpu::FormatStamps({{"vulkan.x.so", 100, 5}, {"meta.json", 7, 6}});
		const std::string b = obgpu::FormatStamps({{"meta.json", 7, 6}, {"vulkan.x.so", 100, 5}});
		Check("stamps: order-independent", a == b && a == "meta.json 7 6\nvulkan.x.so 100 5\n", a);
		Check("stamps: size change differs", a != obgpu::FormatStamps({{"meta.json", 7, 6}, {"vulkan.x.so", 101, 5}}));
		Check("stamps: mtime change differs", a != obgpu::FormatStamps({{"meta.json", 7, 6}, {"vulkan.x.so", 100, 9}}));
		Check("stamps: empty folder", obgpu::FormatStamps({}).empty());
	}
} // namespace

int main()
{
	TestSetting();
	TestMeta();
	TestLibrary();
	TestDecision();
	TestLines();
	TestSelfRead();
	TestStamps();
	std::printf("Passed: %d\nTotal Tests: %d\n", g_passed, g_total);
	return g_passed == g_total ? 0 : 1;
}
