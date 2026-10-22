// ob shim for PCSX2's GS/Renderers/DX12/GSDevice12.h (include/ is searched before PCSX2's tree).
// Same reason as DX11/GSDevice11.h: the RenderAPI::D3D12 branch of GS.cpp's OpenGSDevice() is dead for us.
#pragma once

#include "GS/Renderers/Vulkan/GSDeviceVK.h"

using GSDevice12 = GSDeviceVK;
