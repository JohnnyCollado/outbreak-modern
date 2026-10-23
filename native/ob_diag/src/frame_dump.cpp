#include "frame_dump.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool obDumpFrameNamed(const char *dir, const char *fileName, const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (dir == nullptr || dir[0] == '\0' || fileName == nullptr || fileName[0] == '\0' || rgba == nullptr ||
        width == 0u || height == 0u)
        return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;
    const std::string path = (std::filesystem::path(dir) / fileName).string();
    return stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba,
                          static_cast<int>(width) * 4) != 0;
}

bool obDumpFrame(const char *dir, uint64_t vsyncTick, const uint8_t *rgba, uint32_t width, uint32_t height)
{
    char name[32];
    std::snprintf(name, sizeof(name), "frame-%08llu.png", static_cast<unsigned long long>(vsyncTick));
    return obDumpFrameNamed(dir, name, rgba, width, height);
}
