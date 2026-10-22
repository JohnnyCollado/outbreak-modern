// Config stubs for ob_pcsx2_gs: EmuConfig / GSOptions / EmuFolders, normally from pcsx2/Pcsx2Config.cpp,
// which we do not build (it pulls CDVD, memcards, Pad, USB, audio). The GSOptions members below are copied
// verbatim from PCSX2 v2.9.134 Pcsx2Config.cpp (lines cited); re-copy them when PCSX2 is bumped.
#include "Config.h"

#define OpEqu(field) (field == right.field) // Pcsx2Config.cpp:29

// ---- EmuFolders (pcsx2/Config.h): the embedder sets AppRoot / Resources before GSopen ----
std::string EmuFolders::AppRoot;
std::string EmuFolders::Resources;
std::string EmuFolders::Cache;      // VK shader/pipeline cache dir; the smoke sets DisableShaderCache instead
std::string EmuFolders::Logs;
std::string EmuFolders::Snapshots;
std::string EmuFolders::Videos;

// ---- EmuConfig: GS reads only aspect-ratio, thread-pinning and hotkey fields; the non-GS option
// structs keep zero / in-class defaults (their real constructors live in Pcsx2Config.cpp). ----
Pcsx2Config EmuConfig;

Pcsx2Config::Pcsx2Config() { bitset = 0; }
Pcsx2Config::SpeedhackOptions::SpeedhackOptions() {}
Pcsx2Config::ProfilerOptions::ProfilerOptions() {}
Pcsx2Config::RecompilerOptions::RecompilerOptions() {}
Pcsx2Config::CpuOptions::CpuOptions() {}
Pcsx2Config::GamefixOptions::GamefixOptions() {}
Pcsx2Config::SavestateOptions::SavestateOptions() {}
Pcsx2Config::FilenameOptions::FilenameOptions() {}
Pcsx2Config::EmulationSpeedOptions::EmulationSpeedOptions() {}
Pcsx2Config::USBOptions::USBOptions() {}
Pcsx2Config::PadOptions::PadOptions() {}
Pcsx2Config::AchievementsOptions::AchievementsOptions() {}
Pcsx2Config::SPU2Options::SPU2Options() {}
Pcsx2Config::DEV9Options::DEV9Options() {}
TraceLogFilters::TraceLogFilters() {}
TraceLogsEE::TraceLogsEE() {}
TraceLogsIOP::TraceLogsIOP() {}
TraceLogsMISC::TraceLogsMISC() {}

// ---- Pcsx2Config::GSOptions: verbatim from Pcsx2Config.cpp ----
// Pcsx2Config.cpp:639-645
const char* Pcsx2Config::GSOptions::AspectRatioNames[(size_t)AspectRatioType::MaxCount + 1] = {
	"Stretch",
	"Auto 4:3/3:2",
	"4:3",
	"16:9",
	"10:7",
	nullptr};

// Pcsx2Config.cpp:672
const char* Pcsx2Config::GSOptions::DEFAULT_CAPTURE_CONTAINER = "mp4";

// Pcsx2Config.cpp:686-702
const char* Pcsx2Config::GSOptions::GetRendererName(GSRendererType type)
{
	switch (type)
	{
			// clang-format off
		case GSRendererType::Auto:  return "Auto";
		case GSRendererType::DX11:  return "Direct3D 11";
		case GSRendererType::DX12:  return "Direct3D 12";
		case GSRendererType::Metal: return "Metal";
		case GSRendererType::OGL:   return "OpenGL";
		case GSRendererType::VK:    return "Vulkan";
		case GSRendererType::SW:    return "Software";
		case GSRendererType::Null:  return "Null";
		default:                    return "";
			// clang-format on
	}
}

// Pcsx2Config.cpp:704-707
std::optional<bool> Pcsx2Config::GSOptions::TriStateToOptionalBoolean(int value)
{
	return (value < 0) ? std::optional<bool>(std::nullopt) : std::optional<bool>((value != 0));
}

// Pcsx2Config.cpp:709-792
Pcsx2Config::GSOptions::GSOptions()
{
	bitsets[0] = 0;
	bitsets[1] = 0;

	PCRTCAntiBlur = true;
	DisableInterlaceOffset = false;
	PCRTCOffsets = false;
	PCRTCOverscan = false;
	IntegerScaling = false;
	LinearPresent = GSPostBilinearMode::BilinearSmooth;
	UseDebugDevice = false;
	UseBlitSwapChain = false;
	DisableShaderCache = false;
	DisableFramebufferFetch = false;
	DisableVertexShaderExpand = false;
	SkipDuplicateFrames = true;
	AdvancedFrameDisplay = false;
	OsdMessagesPos = OsdOverlayPos::TopLeft;
	OsdPerformancePos = OsdOverlayPos::TopRight;
	OsdShowSpeed = false;
	OsdShowFPS = false;
	OsdShowVPS = false;
	OsdShowResolution = false;
	OsdShowGSStats = false;
	OsdShowCPU = false;
	OsdShowGPU = false;
	OsdShowGPUDebug = false;
	OsdShowGPUStats = false;
	OsdShowIndicators = true;
	OsdShowFrameTimes = false;
	OsdShowHardwareInfo = false;
	OsdShowVersion = false;
	OsdShowSettings = false;
	OsdshowPatches = false;
	OsdShowInputs = false;
	OsdShowVideoCapture = true;
	OsdShowInputRec = true;
	OsdShowTextureReplacements = false;

	HWDownloadMode = GSHardwareDownloadMode::Enabled;
	HWSpinGPUForReadbacks = false;
	HWSpinCPUForReadbacks = false;
	GPUPaletteConversion = false;
	AutoFlushSW = true;
	PreloadFrameWithGSData = false;
	Mipmap = true;
	HWMipmap = true;
	HWAccurateAlphaTest = false;
	HWAA1 = false;
	UseDebugBlend = false;
	HWROV = false;
	HWROVLogging = false;
	HWROVBarriersVK = false;

	ManualUserHacks = false;
	UserHacks_AlignSpriteX = false;
	UserHacks_AutoFlush = GSHWAutoFlushLevel::Disabled;
	UserHacks_Limit24BitDepth = GSLimit24BitDepth::Disabled;
	UserHacks_CPUFBConversion = false;
	UserHacks_ReadTCOnClose = false;
	UserHacks_DisableDepthSupport = false;
	UserHacks_DisablePartialInvalidation = false;
	UserHacks_DisableSafeFeatures = false;
	UserHacks_DisableRenderFixes = false;
	UserHacks_MergePPSprite = false;
	UserHacks_ForceEvenSpritePosition = false;
	UserHacks_BilinearHack = GSBilinearDirtyMode::Automatic;
	UserHacks_NativePaletteDraw = false;

	DumpReplaceableTextures = false;
	DumpReplaceableMipmaps = false;
	DumpTexturesWithFMVActive = false;
	DumpDirectTextures = true;
	DumpPaletteTextures = true;
	LoadTextureReplacements = false;
	LoadTextureReplacementsAsync = true;
	PrecacheTextureReplacements = false;

	EnableVideoCapture = true;
	EnableVideoCaptureParameters = false;
	EnableAudioCapture = true;
	EnableAudioCaptureParameters = false;
}

// Pcsx2Config.cpp:914-927
bool Pcsx2Config::GSOptions::RestartOptionsAreEqual(const GSOptions& right) const
{
	return OpEqu(Renderer) &&
		   OpEqu(Adapter) &&
		   OpEqu(UseDebugDevice) &&
		   OpEqu(UseBlitSwapChain) &&
		   OpEqu(DisableShaderCache) &&
		   OpEqu(DisableFramebufferFetch) &&
		   OpEqu(DisableVertexShaderExpand) &&
		   OpEqu(OverrideTextureBarriers) &&
		   OpEqu(DepthFeedbackMode) &&
		   OpEqu(HWAA1) &&
		   OpEqu(ExclusiveFullscreenControl);
}

// Pcsx2Config.cpp:1199-1206
bool Pcsx2Config::GSOptions::ShouldDump(u64 draw, int frame) const
{
	int drawOffset = static_cast<int>(draw - static_cast<u64>(SaveDrawStart));
	int frameOffset = frame - SaveFrameStart;
	return DumpGSData &&
		   (drawOffset >= 0) && ((SaveDrawCount < 0) || (drawOffset < SaveDrawCount)) && (drawOffset % SaveDrawBy == 0) &&
		   (frameOffset >= 0) && ((SaveFrameCount < 0) || (frameOffset < SaveFrameCount)) && (frameOffset % SaveFrameBy == 0);
}
