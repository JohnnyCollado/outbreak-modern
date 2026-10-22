// ob shim for PCSX2's GS/Renderers/DX11/GSDevice11.h (include/ is searched before PCSX2's tree).
// ob_pcsx2_gs builds no D3D11 sources. GS.cpp's OpenGSDevice() names GSDevice11 under _WIN32 only to
// make_unique it for RenderAPI::D3D11, which we never request (renderers 13 SW / 14 VK only).
// Aliasing it to the Vulkan device keeps that dead branch compiling without DirectX headers.
#pragma once

#include "GS/Renderers/Vulkan/GSDeviceVK.h"

using GSDevice11 = GSDeviceVK;
