#include "MiniTest.h"
#include "frame_dump.h"
#include "stb_image.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
    std::filesystem::path frameDumpTestDir()
    {
        return std::filesystem::path(OB_DIAG_TEST_DIR) / "frame_dump_test";
    }
}

void register_frame_dump_tests()
{
    MiniTest::Case("FrameDump", [](TestCase &tc)
                   {
        tc.Run("writes frame-<tick>.png with the presented pixels", [](TestCase &t)
               {
            const std::filesystem::path dir = frameDumpTestDir();
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);

            const std::array<uint8_t, 3u * 2u * 4u> pixels = {
                255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u,
                10u, 20u, 30u, 255u, 40u, 50u, 60u, 128u, 70u, 80u, 90u, 0u};
            const std::string dirText = dir.string();
            t.IsTrue(obDumpFrame(dirText.c_str(), 42u, pixels.data(), 3u, 2u), "dump succeeds");

            const std::filesystem::path file = dir / "frame-00000042.png";
            t.IsTrue(std::filesystem::exists(file), "frame-00000042.png exists");

            int w = 0, h = 0, n = 0;
            unsigned char *image = stbi_load(file.string().c_str(), &w, &h, &n, 4);
            t.IsTrue(image != nullptr, "png loads");
            t.Equals(w, 3, "width");
            t.Equals(h, 2, "height");
            if (image != nullptr)
                t.IsTrue(std::memcmp(image, pixels.data(), pixels.size()) == 0, "pixels equal");
            stbi_image_free(image); });

        tc.Run("null dir returns false and writes nothing", [](TestCase &t)
               {
            const std::filesystem::path dir = frameDumpTestDir() / "null_dir";
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);

            const std::array<uint8_t, 4u> pixel = {1u, 2u, 3u, 4u};
            t.IsFalse(obDumpFrame(nullptr, 7u, pixel.data(), 1u, 1u), "null dir fails");
            t.IsFalse(obDumpFrame("", 7u, pixel.data(), 1u, 1u), "empty dir fails");
            t.IsFalse(std::filesystem::exists("frame-00000007.png"), "nothing written to the working dir");
            t.IsFalse(std::filesystem::exists(dir), "nothing written to the test dir"); });
    });
}
