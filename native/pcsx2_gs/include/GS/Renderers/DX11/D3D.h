// ob shim for PCSX2's GS/Renderers/DX11/D3D.h (include/ is searched before PCSX2's tree).
// ob_pcsx2_gs builds no D3D sources; GS.cpp and GSUtil.cpp include this unconditionally on _WIN32.
// Only the three D3D:: functions they call are declared; stubs/gs_stubs.cpp defines them
// (no factory, no adapters, preferred renderer = Vulkan).
#pragma once

#include "common/RedtapeWindows.h"
#include "common/RedtapeWilCom.h"

#include "Config.h"
#include "GS/GS.h"

#include <dxgi1_5.h>
#include <vector>

namespace D3D
{
	wil::com_ptr_nothrow<IDXGIFactory5> CreateFactory(bool debug);
	std::vector<GSAdapterInfo> GetAdapterInfo(IDXGIFactory5* factory);
	GSRendererType GetPreferredRenderer();
} // namespace D3D
