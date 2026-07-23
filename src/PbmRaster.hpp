#pragma once

#include "Core/RasterStreambuf.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

class PbmRaster final : public RasterStreambuf {
public:
    explicit PbmRaster(std::vector<std::filesystem::path> pages);
    std::optional<Capt::PageParams> NextPage() override;

private:
    struct Paper {
        int Width;
        int Height;
        std::uint8_t CaptId;
    };

    std::vector<std::filesystem::path> pages_;
    std::size_t nextPage_ = 0;
    std::vector<char> raster_;

    static std::optional<Paper> MatchPaper(int width, int height);
    static void RotateLandscape(
        std::vector<char>& data,
        int& width,
        int& height
    );
};
