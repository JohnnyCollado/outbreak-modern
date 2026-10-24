// gpu_driver_android: picks the Vulkan driver PCSX2's GS opens on Android (outbreak.ini gpu_driver; gpu_driver.h).
// Call on the GS thread, before GSopen. Never fails: anything wrong with a custom driver logs one
// "[gpu] custom driver <name> unavailable (<reason>); using system driver" line and leaves PCSX2 on the system driver.
#pragma once

#include <string>

#include "gpu_driver.h"

// setting: the gpu_driver value ("system", empty, or a folder name). Logs "[gpu] driver=..." for the driver chosen.
// True when a custom driver's libvulkan handle was handed to PCSX2 (Vulkan::SetPreloadedVulkanLibrary).
bool ObGpuSelectDriver(const std::string& setting);
// After GSopen failed with the custom driver: logs the fall-back line, hands PCSX2 the system library again and
// logs the system driver line. Call while PCSX2's Vulkan library is not loaded (after the failed GSopen).
void ObGpuDropCustom(const char* reason);
// The device of the driver PCSX2 opens next, as last probed by the two calls above (null = the probe failed).
const obgpu::DeviceInfo* ObGpuDevice();
