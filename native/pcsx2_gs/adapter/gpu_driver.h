// gpu_driver: the GPU driver choice for PCSX2's Vulkan GS on Android (outbreak.ini "gpu_driver = system | <name>").
// This header and gpu_driver.cpp are the pure part (no Vulkan, no Android): setting parsing, the driver package's
// meta.json, the folder sync stamps, the fall-back decision and the log lines. Host-tested by tests/gpu_driver_tests.cpp.
// The device part is gpu_driver_android.cpp (libadrenotools, Vulkan probe, PCSX2's preloaded-library hook,
// upstream/pcsx2/0002). Layout and steps: docs/ops/android.md "GPU driver".
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace obgpu
{
	// "system" (or empty, any case) = the phone's own driver; anything else names a folder under drivers/.
	struct DriverChoice
	{
		bool system = true;
		std::string name;  // custom: the folder name
		std::string error; // non-empty: the value was rejected (system is used)
	};
	DriverChoice ParseGpuDriverSetting(std::string_view value);
	// 1..64 characters of [A-Za-z0-9._-], not starting with '.', not "system": a plain folder name, no path.
	bool IsValidDriverName(std::string_view name);

	// A driver package's meta.json (libadrenotools tools/ADPKG.md; the K11MCH1/AdrenoToolsDrivers Turnip packages use
	// it too). Only the fields we read; unknown keys, arrays and nested objects are skipped.
	struct DriverMeta
	{
		int schemaVersion = 0;
		std::string name, description, author, packageVersion, vendor, driverVersion, libraryName;
		int minApi = 0;
	};
	// False with a reason when the text is not one JSON object.
	bool ParseDriverMeta(std::string_view json, DriverMeta& meta, std::string& error);
	// The driver's main library: meta.libraryName when set (a plain file name present in files), else the one
	// "vulkan.*.so" among files. Empty + error when neither.
	std::string PickDriverLibrary(const DriverMeta& meta, const std::vector<std::string>& files, std::string& error);

	// What the device side found, for the one decision below.
	struct CustomDriverCheck
	{
		bool folder_found = false;          // drivers/<name>/ exists (external or internal files dir)
		bool meta_found = false;            // meta.json readable
		bool meta_ok = false;               // ParseDriverMeta succeeded
		std::string meta_error;
		DriverMeta meta;
		std::vector<std::string> files;     // regular files in the folder
		int device_api = 0;                 // android_get_device_api_level()
		uint32_t system_vendor_id = 0;      // vendorID of the system driver's first device; 0 = probe failed
		bool kgsl_present = false;          // /dev/kgsl-3d0 (Adreno's kernel driver): decides when the probe failed
		bool hooks_present = false;         // libadrenotools' libhook_impl.so + libmain_hook.so in the native lib dir
		bool native_lib_dir_ok = false;     // the APK's libraries were extracted (a real directory)
	};
	// Empty = load the custom driver. Otherwise the reason it is not loaded (for FallbackLine). library = the file.
	std::string CustomDriverBlocker(const CustomDriverCheck& c, std::string& library);
	// Non-empty when the package states a newer Android (meta minApi) than the device: not a blocker (the driver is
	// loaded anyway, with the usual fall-back), only a warning for the log and the launcher.
	std::string MinApiWarning(const CustomDriverCheck& c);

	// "[gpu] custom driver <name> unavailable (<reason>); using system driver"
	std::string FallbackLine(std::string_view name, std::string_view reason);

	struct DeviceInfo
	{
		uint32_t vendor_id = 0, device_id = 0, driver_version = 0, api_version = 0;
		std::string device_name, driver_name, driver_info; // driver_*: VkPhysicalDeviceDriverProperties, may be empty
	};
	std::string VendorName(uint32_t vendor_id); // "Qualcomm" (0x5143), "ARM" (0x13B5), ... else "0x%04X"
	// The vendor's encoding of driverVersion: NVIDIA 10.8.8.6 bits, else VK_MAKE_VERSION 10.10.12; plus the raw hex.
	std::string FormatDriverVersion(uint32_t vendor_id, uint32_t version);
	// "[gpu] driver=system|custom:<name> vendor=... deviceName=... driverVersion=... (driverName driverInfo) api=1.3.x"
	std::string DriverLine(bool custom, std::string_view name, const DeviceInfo& d);

	constexpr uint32_t kVendorQualcomm = 0x5143;

	// How PCSX2's Vulkan renderer reads the colour already drawn when a draw blends with it (outbreak.ini
	// gs_self_read, PS2Recomp patch 0054). PCSX2 2.9.134 picks the in-pass read (rasterization-order framebuffer
	// fetch where the driver offers it, else a texture barrier, GSDeviceVK.cpp CheckFeatures) on every device.
	// Qualcomm's own Adreno driver returns stale colour there: J's Bar's light glows come out black on the Odin
	// (reports/2026-10-24-android.md entries 11-12). ARMSX2 reads the destination from a copy of the target on
	// Adreno and gates fetch off on the Qualcomm driver for Adreno 8xx (its GSDeviceVK.cpp, 90126bd, :4030-4146).
	enum class SelfRead
	{
		Auto,    // copy on Qualcomm's own driver, barrier for Turnip on Adreno 8xx, PCSX2's choice elsewhere
		Copy,    // texture barriers off: the renderer reads a copy of the target (OverrideTextureBarriers = 0)
		Barrier, // in-pass read with texture barriers, no framebuffer fetch
		Fetch,   // PCSX2's choice: framebuffer fetch where offered, else barriers
	};
	struct SelfReadChoice
	{
		SelfRead mode = SelfRead::Auto;
		std::string error; // non-empty: the value was rejected (Auto is used)
	};
	// "auto" (or empty), "copy", "barrier", "fetch"; any case and surrounding blanks.
	SelfReadChoice ParseSelfReadSetting(std::string_view value);
	// Qualcomm's proprietary driver (not Turnip or another Mesa build) on a Qualcomm GPU. Without driver properties
	// (no driverName) a Qualcomm GPU counts as the proprietary driver: Mesa's drivers always report a name.
	bool IsQualcommProprietary(const DeviceInfo& d);
	// Turnip (Mesa) on an Adreno 8xx (the 8 Elite's Adreno 830): its framebuffer fetch draws stale colour, title text
	// and J's Bar's glows black; barriers draw them right (reports/2026-10-24-android.md entry 17).
	bool IsTurnipAdreno8(const DeviceInfo& d);
	// The two PCSX2 GS options it sets, and the log line's road and reason.
	struct SelfReadDecision
	{
		int override_texture_barriers = -1;    // GSOptions::OverrideTextureBarriers (-1 = PCSX2's automatic)
		bool disable_framebuffer_fetch = false; // GSOptions::DisableFramebufferFetch
		std::string road;                        // "copy", "barrier", "fetch" (PCSX2's choice)
		std::string why;                         // "auto: Qualcomm's driver" / "auto" / "outbreak.ini"
	};
	// device: the driver PCSX2 opens (null = not probed, e.g. on desktop: Auto leaves PCSX2's choice).
	SelfReadDecision DecideSelfRead(SelfRead setting, const DeviceInfo* device);
	// "[gpu] self-read=<road> (<why>)"
	std::string SelfReadLine(const SelfReadDecision& d);

	// Folder sync from the external files dir (where the player drops the package) to the internal one (the only
	// place a driver may be dlopen'ed from): one "name size mtime" line per file, sorted by name.
	struct FileStamp
	{
		std::string name;
		uint64_t size = 0;
		int64_t mtime = 0;
	};
	std::string FormatStamps(std::vector<FileStamp> files);
} // namespace obgpu
