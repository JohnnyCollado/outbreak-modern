// UI stubs for ob_pcsx2_gs: ImGuiManager / FullscreenUI (pcsx2/ImGui/*, not built), imgui's FreeType
// font loader (imgui_freetype.cpp, not built: no FreeType), and common/Image.cpp's RGBA8Image I/O.
// The GS devices still render ImGui every present (GSDeviceVK::RenderImGui), so ImGuiManager keeps a
// real, empty ImGui context: default font only, no OSD, no windows.
#include "GS/Renderers/Common/GSDevice.h"
#include "ImGui/FullscreenUI.h"
#include "ImGui/ImGuiManager.h"

#include "common/Image.h"
#include "common/Timer.h"

#include "imgui.h"
#include "imgui_freetype.h"
#include "imgui_internal.h"

// ---- imgui: IMGUI_ENABLE_FREETYPE is set in PCSX2's imconfig.h, so imgui asks this for its default
// loader; we compile with IMGUI_ENABLE_STB_TRUETYPE and hand back the stb_truetype loader. ----
const ImFontLoader* ImGuiFreeType::GetFontLoader() { return ImFontAtlasGetFontLoaderForStbTruetype(); }

// ---- ImGuiManager (pcsx2/ImGui/ImGuiManager.cpp), minimal subset of Initialize/NewFrame/Shutdown ----
static Common::Timer s_last_render_time;

bool ImGuiManager::Initialize()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	io.DisplayFramebufferScale = ImVec2(1, 1);
	io.DisplaySize = ImVec2(static_cast<float>(g_gs_device->GetWindowWidth()), static_cast<float>(g_gs_device->GetWindowHeight()));
	io.Fonts->AddFontDefault();
	NewFrame();
	return true;
}

void ImGuiManager::Shutdown(bool)
{
	if (g_gs_device)
		g_gs_device->DestroyImGuiTextures();
	if (ImGui::GetCurrentContext())
		ImGui::DestroyContext();
}

void ImGuiManager::WindowResized()
{
	if (ImGui::GetCurrentContext())
		ImGui::GetIO().DisplaySize =
			ImVec2(static_cast<float>(g_gs_device->GetWindowWidth()), static_cast<float>(g_gs_device->GetWindowHeight()));
}

void ImGuiManager::RequestScaleUpdate() {}
void ImGuiManager::ReloadFonts() {}

void ImGuiManager::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	io.DeltaTime = std::max(static_cast<float>(s_last_render_time.GetTimeSecondsAndReset()), 1e-4f);
	ImGui::NewFrame();
}

void ImGuiManager::SkipFrame()
{
	ImGui::EndFrame();
	NewFrame();
}

void ImGuiManager::RenderOSD() {}

// ---- FullscreenUI (pcsx2/ImGui/FullscreenUI.cpp) ----
void FullscreenUI::Render() {}

// ---- RGBA8Image (common/Image.cpp: libpng/libjpeg/libwebp). In-memory parts as upstream, no file I/O. ----
RGBA8Image::RGBA8Image() = default;
RGBA8Image::RGBA8Image(RGBA8Image&& move) : Image(move) {} // Image.cpp:77-80 (sic: copies, as upstream)
bool RGBA8Image::SaveToFile(const char*, u8) const { return false; }
