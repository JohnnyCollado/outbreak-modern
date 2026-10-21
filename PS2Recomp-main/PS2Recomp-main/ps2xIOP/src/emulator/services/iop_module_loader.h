#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2x::iop
{
    class IopHost;
}

namespace ps2x::iop::detail
{
    class IopMemory;

    enum class IopImageLoadError : uint8_t
    {
        None,
        InvalidElf,
        ArenaExhausted,
        MalformedImage,
    };

    struct IopImageLoadResult
    {
        IopImageLoadError error = IopImageLoadError::MalformedImage;
        uint32_t base = 0;
        uint32_t size = 0;
        uint32_t entry = 0;
        uint32_t gp = 0;
        uint32_t nextModuleCursor = 0;
        bool relocationsComplete = true;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == IopImageLoadError::None;
        }
    };

    class IopModuleLoader
    {
    public:
        [[nodiscard]] static bool readWholeHostFile(IopHost &host, std::string_view guestPath, std::vector<uint8_t> &bytes);
        [[nodiscard]] static bool readElfFromGuest(IopHost &host, uint32_t guestAddress, std::vector<uint8_t> &bytes);
        // The same, from IOP RAM: where SifLoadModuleBuffer's buffer lives on a real PS2.
        [[nodiscard]] static bool readElfFromIop(const IopMemory &memory, uint32_t iopAddress, std::vector<uint8_t> &bytes);
        [[nodiscard]] static IopImageLoadResult load(std::span<const uint8_t> image, IopMemory &memory, uint32_t moduleCursor);
        // The module name stored in the IRX's .iopmod header, or "" when there is none.
        [[nodiscard]] static std::string moduleName(std::span<const uint8_t> image);

    private:
        [[nodiscard]] static bool readElf(const std::function<bool(uint32_t, void *, size_t)> &read, uint32_t guestAddress, std::vector<uint8_t> &bytes);
    };
}
