// gpu_driver_android: the device part of the Android GPU driver choice (gpu_driver_android.h, gpu_driver.h).
//
// Custom driver = a driver package folder (meta.json + the driver .so, libadrenotools tools/ADPKG.md) the player puts
// in <external files dir>/drivers/<name>/. dlopen() refuses libraries on shared storage (libadrenotools driver.h,
// customDriverDir), so the folder is copied to <internal files dir>/drivers/<name>/ when it changed (".ob-stamps").
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

	bool MakeDirs(const std::string& path)
	{
		for (size_t i = 1; i <= path.size(); ++i)
		{
			if (i == path.size() || path[i] == '/')
			{
				const std::string part = path.substr(0, i);
				if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
					return false;
			}
		}
		return true;
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

	bool WriteText(const std::string& path, const std::string& text)
	{
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f)
			return false;
		const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
		return std::fclose(f) == 0 && ok;
	}

	bool CopyFile(const std::string& from, const std::string& to)
	{
		const int in = open(from.c_str(), O_RDONLY | O_CLOEXEC);
		if (in < 0)
			return false;
		const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		bool ok = out >= 0;
		char buf[1 << 16];
		while (ok)
		{
			const ssize_t r = read(in, buf, sizeof(buf));
			if (r == 0)
				break;
			if (r < 0)
			{
				ok = errno == EINTR;
				continue;
			}
			for (ssize_t w = 0; ok && w < r;)
			{
				const ssize_t k = write(out, buf + w, static_cast<size_t>(r - w));
				if (k < 0)
					ok = errno == EINTR;
				else
					w += k;
			}
		}
		close(in);
		if (out >= 0 && close(out) != 0)
			ok = false;
		return ok;
	}

	// external -> internal when the stamps differ. False + reason on a failed copy.
	bool SyncFolder(const std::string& name, const std::string& ext, const std::string& internal, std::string& reason)
	{
		const std::vector<obgpu::FileStamp> src = ListFiles(ext);
		const std::string want = obgpu::FormatStamps(src);
		std::string have;
		const std::string stamps = internal + "/.ob-stamps";
		if (IsDir(internal) && ReadText(stamps, have, 1 << 20) && have == want)
			return true;
		if (!MakeDirs(internal))
		{
			reason = "cannot create " + internal;
			return false;
		}
		unlink(stamps.c_str());
		for (const obgpu::FileStamp& f : ListFiles(internal))
			unlink((internal + "/" + f.name).c_str());
		uint64_t bytes = 0;
		for (const obgpu::FileStamp& f : src)
		{
			if (!CopyFile(ext + "/" + f.name, internal + "/" + f.name))
			{
				reason = "cannot copy " + f.name + " to " + internal + " (" + std::strerror(errno) + ")";
				return false;
			}
			bytes += f.size;
		}
		WriteText(stamps, want);
		char b[160];
		std::snprintf(b, sizeof(b), "[gpu] driver %s: copied %zu file(s), %.1f MB, to ", name.c_str(), src.size(),
			static_cast<double>(bytes) / (1024.0 * 1024.0));
		Log(b + internal);
		return true;
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
	const char* ext_root = (app && app->activity) ? app->activity->externalDataPath : nullptr;
	const char* int_root = (app && app->activity) ? app->activity->internalDataPath : nullptr;
	if (!int_root)
	{
		Log(obgpu::FallbackLine(name, "no internal files dir"));
		LogSystemDriver();
		return false;
	}
	const std::string ext = ext_root ? std::string(ext_root) + "/drivers/" + name : std::string();
	const std::string internal = std::string(int_root) + "/drivers/" + name;

	obgpu::CustomDriverCheck c;
	std::string reason;
	if (!ext.empty() && IsDir(ext))
	{
		if (!SyncFolder(name, ext, internal, reason))
		{
			Log(obgpu::FallbackLine(name, reason));
			LogSystemDriver();
			return false;
		}
	}
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
