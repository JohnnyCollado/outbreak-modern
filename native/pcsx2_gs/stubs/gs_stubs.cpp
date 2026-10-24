// GS feature stubs for ob_pcsx2_gs: the GS sources excluded in CMakeLists.txt (capture, dumps, PNG,
// texture replacements) plus the D3D:: entry points GS.cpp / GSUtil.cpp call on _WIN32.
// Every feature reports "off / not present"; none of them is on the boot path.
#include "GS/GS.h"
#include "GS/GSCapture.h"
#include "GS/GSDump.h"
#include "GS/GSPng.h"
#if defined(_WIN32)
#include "GS/Renderers/DX11/D3D.h" // ob shim (include/), not PCSX2's
#endif
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include "GSDumpReplayer.h"

// ---- GSCapture (GS/GSCapture.cpp: FFmpeg) ----
bool GSCapture::BeginCapture(float, GSVector2i, float, std::string) { return false; }
bool GSCapture::DeliverVideoFrame(GSTexture*) { return false; }
void GSCapture::EndCapture() {}
bool GSCapture::IsCapturing() { return false; }
bool GSCapture::IsCapturingVideo() { return false; }
GSVector2i GSCapture::GetSize() { return GSVector2i(0, 0); }
std::string GSCapture::GetNextCaptureFileName() { return {}; }
void GSCapture::Flush() {}

// ---- GS dumps (GS/GSDump.cpp, GS/GSLzma.cpp, pcsx2/GSDumpReplayer.cpp) ----
std::unique_ptr<GSDumpBase> GSDumpBase::CreateUncompressedDump(const std::string&, const std::string&, u32, u32, u32,
	const u32*, const freezeData&, const GSPrivRegSet*) { return nullptr; }
std::unique_ptr<GSDumpBase> GSDumpBase::CreateXzDump(const std::string&, const std::string&, u32, u32, u32,
	const u32*, const freezeData&, const GSPrivRegSet*) { return nullptr; }
std::unique_ptr<GSDumpBase> GSDumpBase::CreateZstDump(const std::string&, const std::string&, u32, u32, u32,
	const u32*, const freezeData&, const GSPrivRegSet*) { return nullptr; }
// Never reached: no dump object is ever created (the factories above return null).
void GSDumpBase::ReadFIFO(u32) {}
void GSDumpBase::Transfer(int, const u8*, size_t) {}
bool GSDumpBase::VSync(int, bool, const GSPrivRegSet*) { return false; }

bool GSDumpReplayer::IsReplayingDump() { return false; }
bool GSDumpReplayer::IsRunner() { return false; }
void GSDumpReplayer::RenderUI() {}
void GSDumpReplayer::UpdateGSStats() {}

// ---- GSPng (GS/GSPng.cpp: libpng) ----
bool GSPng::Save(GSPng::Format, const std::string&, const u8*, int, int, int, int, bool) { return false; }

#if !defined(OB_TEXTURE_REPLACEMENTS)
// ---- GSTextureReplacements (GS/Renderers/HW/GSTextureReplacements*.cpp) ----
// Built for real on Windows (OB_TEXTURE_REPLACEMENTS, libpng + zlib + libzip); stubbed on Android for now.
void GSTextureReplacements::Initialize() {}
void GSTextureReplacements::GameChanged() {}
void GSTextureReplacements::ReloadReplacementMap() {}
void GSTextureReplacements::UpdateConfig(Pcsx2Config::GSOptions&) {}
void GSTextureReplacements::Shutdown() {}
bool GSTextureReplacements::HasAnyReplacementTextures() { return false; }
bool GSTextureReplacements::HasReplacementTextureWithOtherPalette(const GSTextureCache::HashCacheKey&) { return false; }
GSTexture* GSTextureReplacements::LookupReplacementTexture(const GSTextureCache::HashCacheKey&, bool, bool* pending,
	std::pair<u8, u8>*)
{
	if (pending)
		*pending = false;
	return nullptr;
}
void GSTextureReplacements::ProcessAsyncLoadedTextures() {}
void GSTextureReplacements::DumpTexture(const GSTextureCache::HashCacheKey&, const GIFRegTEX0&, const GIFRegTEXA&,
	GSTextureCache::SourceRegion, GSLocalMemory&, u32) {}
#endif

#if defined(_WIN32)
// ---- D3D:: (GS/Renderers/DX11/D3D.cpp, not built): no DXGI; Auto/SW resolve to Vulkan ----
wil::com_ptr_nothrow<IDXGIFactory5> D3D::CreateFactory(bool) { return {}; }
std::vector<GSAdapterInfo> D3D::GetAdapterInfo(IDXGIFactory5*) { return {}; }
// GSUtil::GetPreferredRenderer() asks this on Windows; the SW renderer presents through it too.
GSRendererType D3D::GetPreferredRenderer() { return GSRendererType::VK; }
#endif
