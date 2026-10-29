// gpu_driver_android: the device part of the Android GPU driver choice (gpu_driver_android.h, gpu_driver.h).
//
// Custom driver = a driver package folder (meta.json + the driver .so, libadrenotools tools/ADPKG.md) the launcher
// imports into <internal files dir>/drivers/<name>/ (internal only: security queue C2; dlopen() also refuses libraries
// on shared storage, libadrenotools driver.h customDriverDir).
// libadrenotools (BSD-2-Clause, github.com/bylaws/libadrenotools) then opens a private copy of the system
// libvulkan.so whose driver load is redirected to the custom .so; its hooks libhook_impl.so + libmain_hook.so are in
// the APK's lib/ (nativeLibraryDir, extractNativeLibs=true). We probe the handle (instance with the surface
// extensions PCSX2 asks for, >= 1 Vulkan 1.1 device) before handing it to PCSX2 (upstream/pcsx2/0002). The handle is
// never closed: PCSX2 re-opens Vulkan whenever it re-creates the device, and closing a namespaced driver is not worth
// the risk; a refused handle is leaked too.
#if defined(__ANDROID__)

#include "gpu_driver_android.h"
#include "gpu_driver.h"

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include "vulkan/vulkan.h"

#include <adrenotools/driver.h>
#include <android/api-level.h>
#include <android_native_app_glue.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" struct android_app* GetAndroidApp(void);

namespace Vulkan
{
	void SetPreloadedVulkanLibrary(void* handle); // upstream/pcsx2/0002 (VKLoader.cpp)
}

namespace
{
	std::string s_custom_name; // the custom driver handed to PCSX2; empty = system
	obgpu::DeviceInfo s_device; // what the last probe of the driver PCSX2 opens found (ObGpuDevice)
	bool s_device_ok = false;

	void Log(const std::string& line)
	{
		std::fprintf(stderr, "%s\n", line.c_str());
		std::fflush(stderr);
		// The settings menu shows what ran last time (plan P11): this run's [gpu] lines in <external files>/gpu-last-run.txt.
		static bool s_first = true;
		android_app* app = GetAndroidApp();
		if (app && app->activity && app->activity->externalDataPath)
		{
			const std::string path = std::string(app->activity->externalDataPath) + "/gpu-last-run.txt";
			if (std::FILE* f = std::fopen(path.c_str(), s_first ? "w" : "a"))
			{
				std::fprintf(f, "%s\n", line.c_str());
				std::fclose(f);
			}
			s_first = false;
		}
	}

	// Instance with VK_KHR_surface + VK_KHR_android_surface (GSDeviceVK requires both for an Android window),
	// first physical device, its properties and (Vulkan 1.2 / VK_KHR_driver_properties) driver properties.
	bool Probe(void* lib, obgpu::DeviceInfo& d, std::string& reason)
	{
		auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
		if (!gipa)
		{
			reason = "no vkGetInstanceProcAddr";
			return false;
		}
		auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
		auto enum_ext = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
			gipa(nullptr, "vkEnumerateInstanceExtensionProperties"));
		if (!create || !enum_ext)
		{
			reason = "no vkCreateInstance";
			return false;
		}
		uint32_t n = 0;
		enum_ext(nullptr, &n, nullptr);
		std::vector<VkExtensionProperties> exts(n);
		if (n)
			enum_ext(nullptr, &n, exts.data());
		bool surface = false, android_surface = false;
		for (const VkExtensionProperties& e : exts)
		{
			surface |= std::strcmp(e.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
			android_surface |= std::strcmp(e.extensionName, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME) == 0;
		}
		if (!surface || !android_surface)
		{
			reason = "no VK_KHR_android_surface";
			return false;
		}
		VkApplicationInfo app = {};
		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.pApplicationName = "outbreak-modern gpu probe";
		app.apiVersion = VK_API_VERSION_1_1; // as GSDeviceVK::CreateVulkanInstance
		const char* names[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
		VkInstanceCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		ci.pApplicationInfo = &app;
		ci.enabledExtensionCount = 2;
		ci.ppEnabledExtensionNames = names;
		VkInstance inst = VK_NULL_HANDLE;
		const VkResult r = create(&ci, nullptr, &inst);
		if (r != VK_SUCCESS || inst == VK_NULL_HANDLE)
		{
			char b[64];
			std::snprintf(b, sizeof(b), "vkCreateInstance failed (VkResult %d)", static_cast<int>(r));
			reason = b;
			return false;
		}
		auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(inst, "vkDestroyInstance"));
		auto enum_dev = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(inst, "vkEnumeratePhysicalDevices"));
		auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(inst, "vkGetPhysicalDeviceProperties"));
		auto props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(inst, "vkGetPhysicalDeviceProperties2"));
		auto dev_ext = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
			gipa(inst, "vkEnumerateDeviceExtensionProperties"));
		bool ok = false;
		uint32_t count = 0;
		if (!destroy || !enum_dev || !props)
			reason = "instance functions missing";
		else if (enum_dev(inst, &count, nullptr) != VK_SUCCESS || count == 0)
			reason = "no Vulkan device";
		else
		{
			std::vector<VkPhysicalDevice> devs(count);
			enum_dev(inst, &count, devs.data());
			VkPhysicalDeviceProperties p = {};
			props(devs[0], &p);
			d.vendor_id = p.vendorID;
			d.device_id = p.deviceID;
			d.driver_version = p.driverVersion;
			d.api_version = p.apiVersion;
			d.device_name = p.deviceName;
			bool driver_props = p.apiVersion >= VK_API_VERSION_1_2;
			if (!driver_props && dev_ext)
			{
				uint32_t m = 0;
				dev_ext(devs[0], nullptr, &m, nullptr);
				std::vector<VkExtensionProperties> de(m);
				if (m)
					dev_ext(devs[0], nullptr, &m, de.data());
				for (const VkExtensionProperties& e : de)
					driver_props |= std::strcmp(e.extensionName, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME) == 0;
			}
			if (driver_props && props2)
			{
				VkPhysicalDeviceDriverProperties drv = {};
				drv.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
				VkPhysicalDeviceProperties2 p2 = {};
				p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
				p2.pNext = &drv;
				props2(devs[0], &p2);
				d.driver_name.assign(drv.driverName, strnlen(drv.driverName, sizeof(drv.driverName)));
				d.driver_info.assign(drv.driverInfo, strnlen(drv.driverInfo, sizeof(drv.driverInfo)));
			}
			if (p.apiVersion < VK_API_VERSION_1_1)
				reason = "device is Vulkan 1.0; PCSX2 needs 1.1";
			else
				ok = true;
		}
		if (destroy)
			destroy(inst, nullptr);
		return ok;
	}

	void LogSystemDriver()
	{
		void* sys = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL); // kept open: PCSX2 opens it next anyway
		obgpu::DeviceInfo d;
		std::string reason;
		s_device_ok = sys && Probe(sys, d, reason);
		if (s_device_ok)
		{
			s_device = d;
			Log(obgpu::DriverLine(false, "", d));
		}
		else
			Log("[gpu] driver=system (probe failed: " + (sys ? reason : std::string("dlopen libvulkan.so: ") + dlerror()) + ")");
	}

	bool IsDir(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	}
	bool IsFile(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
	}

	// Regular files directly in dir (not "." files: .ob-stamps is ours).
	std::vector<obgpu::FileStamp> ListFiles(const std::string& dir)
	{
		std::vector<obgpu::FileStamp> out;
		DIR* d = opendir(dir.c_str());
		if (!d)
			return out;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.')
				continue;
			struct stat st;
			const std::string p = dir + "/" + e->d_name;
			if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode))
				out.push_back({e->d_name, static_cast<uint64_t>(st.st_size), static_cast<int64_t>(st.st_mtime)});
		}
		closedir(d);
		return out;
	}

	bool ReadText(const std::string& path, std::string& out, size_t limit)
	{
		out.clear();
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f)
			return false;
		char buf[4096];
		size_t got;
		while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < limit)
			out.append(buf, got);
		std::fclose(f);
		return out.size() <= limit;
	}

	// The directory our own .so was loaded from = nativeLibraryDir when the APK's libraries are extracted.
	std::string NativeLibDir()
	{
		Dl_info info = {};
		if (!dladdr(reinterpret_cast<void*>(&ObGpuSelectDriver), &info) || !info.dli_fname)
			return {};
		const std::string so = info.dli_fname;
		if (so.find("!/") != std::string::npos) // loaded straight from the APK
			return {};
		const size_t slash = so.rfind('/');
		return slash == std::string::npos ? std::string() : so.substr(0, slash);
	}
} // namespace

bool ObGpuSelectDriver(const std::string& setting)
{
	s_custom_name.clear();
	const obgpu::DriverChoice choice = obgpu::ParseGpuDriverSetting(setting);
	if (!choice.error.empty())
		Log("[gpu] " + choice.error + "; using system driver");
	if (choice.system)
	{
		LogSystemDriver();
		return false;
	}
	const std::string& name = choice.name;
	android_app* app = GetAndroidApp();
	const char* int_root = (app && app->activity) ? app->activity->internalDataPath : nullptr;
	if (!int_root)
	{
		Log(obgpu::FallbackLine(name, "no internal files dir"));
		LogSystemDriver();
		return false;
	}
	// Internal storage only (security queue C2): the launcher imports drivers there; nothing is taken from the external
	// folder, which other apps may write on Android 10.
	const std::string internal = std::string(int_root) + "/drivers/" + name;

	obgpu::CustomDriverCheck c;
	std::string reason;
	c.folder_found = IsDir(internal);
	if (c.folder_found)
	{
		for (const obgpu::FileStamp& f : ListFiles(internal))
			c.files.push_back(f.name);
		std::string meta;
		c.meta_found = ReadText(internal + "/meta.json", meta, 1 << 16);
		if (c.meta_found)
			c.meta_ok = obgpu::ParseDriverMeta(meta, c.meta, c.meta_error);
	}
	c.device_api = android_get_device_api_level();
	c.kgsl_present = access("/dev/kgsl-3d0", F_OK) == 0;
	// No system probe here: the system driver stays out of the process when the custom one loads.
	const std::string libdir = NativeLibDir();
	c.native_lib_dir_ok = !libdir.empty() && IsDir(libdir);
	c.hooks_present = c.native_lib_dir_ok && IsFile(libdir + "/libhook_impl.so") && IsFile(libdir + "/libmain_hook.so");
	std::string library;
	reason = obgpu::CustomDriverBlocker(c, library);
	if (const std::string warn = obgpu::MinApiWarning(c); !warn.empty() && reason.empty())
		Log("[gpu] custom driver " + name + ": " + warn);
	if (!reason.empty())
	{
		Log(obgpu::FallbackLine(name, reason));
		LogSystemDriver();
		return false;
	}
	Log("[gpu] driver " + name + ": " + (c.meta.name.empty() ? library : c.meta.name) +
		(c.meta.driverVersion.empty() ? std::string() : " " + c.meta.driverVersion) + " (" + internal + "/" + library + ")");

	dlerror();
	void* handle = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, nullptr, libdir.c_str(),
		(internal + "/").c_str(), library.c_str(), nullptr, nullptr);
	if (!handle)
	{
		const char* e = dlerror();
		Log(obgpu::FallbackLine(name, std::string("adrenotools_open_libvulkan failed: ") + (e ? e : "no dlerror")));
		LogSystemDriver();
		return false;
	}
	obgpu::DeviceInfo d;
	if (!Probe(handle, d, reason))
	{
		Log(obgpu::FallbackLine(name, reason)); // handle leaked on purpose (file comment)
		LogSystemDriver();
		return false;
	}
	Vulkan::SetPreloadedVulkanLibrary(handle);
	s_custom_name = name;
	s_device = d;
	s_device_ok = true;
	Log(obgpu::DriverLine(true, name, d));
	return true;
}

const obgpu::DeviceInfo* ObGpuDevice()
{
	return s_device_ok ? &s_device : nullptr;
}

void ObGpuDropCustom(const char* reason)
{
	if (s_custom_name.empty())
		return;
	Log(obgpu::FallbackLine(s_custom_name, reason ? reason : "PCSX2 could not open it"));
	Vulkan::SetPreloadedVulkanLibrary(nullptr);
	s_custom_name.clear();
	LogSystemDriver();
}

#endif // __ANDROID__
