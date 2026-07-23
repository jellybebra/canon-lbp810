#include "PbmRaster.hpp"

#include <libcapt/Protocol/Enums.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
    std::string readToken(std::istream& input) {
        std::string token;
        while (true) {
            input >> std::ws;
            if (input.peek() != '#') {
                break;
            }
            std::string ignored;
            std::getline(input, ignored);
        }
        input >> token;
        if (!input) {
            throw std::runtime_error("invalid PBM header");
        }
        return token;
    }

    bool bitAt(const std::vector<char>& data, int stride, int x, int y) {
        const auto value = static_cast<unsigned char>(
            data[static_cast<std::size_t>(y) * stride + x / 8]
        );
        return (value & (0x80u >> (x % 8))) != 0;
    }

    void setBit(std::vector<char>& data, int stride, int x, int y) {
        auto& value = reinterpret_cast<unsigned char&>(
            data[static_cast<std::size_t>(y) * stride + x / 8]
        );
        value |= static_cast<unsigned char>(0x80u >> (x % 8));
    }
}

PbmRaster::PbmRaster(std::vector<std::filesystem::path> pages)
    : pages_(std::move(pages)) {}

std::optional<PbmRaster::Paper> PbmRaster::MatchPaper(int width, int height) {
    static constexpr std::array<Paper, 15> papers{{
        {5100, 6600, 0x0d}, // Letter
        {6600, 10200, 0x0b}, // Ledger
        {5100, 8400, 0x0c}, // Legal
        {4350, 6300, 0x0a}, // Executive
        {7014, 9920, 0x01}, // A3
        {4960, 7014, 0x02}, // A4
        {3506, 4960, 0x03}, // A5
        {6070, 8598, 0x06}, // B4
        {4298, 6070, 0x07}, // B5
        {2480, 5692, 0x16}, // Envelope #10
        {2598, 5196, 0x18}, // Envelope DL
        {3826, 5408, 0x15}, // Envelope C5
        {2314, 4510, 0x17}, // Monarch
        {2362, 3506, 0x0e}, // Postcard
        {3506, 4724, 0x0f}, // Double postcard
    }};

    auto distance = [&](const Paper& paper) {
        return std::abs(width - paper.Width) + std::abs(height - paper.Height);
    };
    auto match = std::ranges::min_element(
        papers,
        {},
        distance
    );
    if (match != papers.end()
        && std::abs(width - match->Width) <= 40
        && std::abs(height - match->Height) <= 40) {
        return *match;
    }
    return std::nullopt;
}

void PbmRaster::RotateLandscape(
    std::vector<char>& data,
    int& width,
    int& height
) {
    const int sourceStride = (width + 7) / 8;
    const int destinationWidth = height;
    const int destinationHeight = width;
    const int destinationStride = (destinationWidth + 7) / 8;
    std::vector<char> rotated(
        static_cast<std::size_t>(destinationStride) * destinationHeight,
        0
    );

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (bitAt(data, sourceStride, x, y)) {
                setBit(rotated, destinationStride, height - 1 - y, x);
            }
        }
    }
    data = std::move(rotated);
    width = destinationWidth;
    height = destinationHeight;
}

std::optional<Capt::PageParams> PbmRaster::NextPage() {
    if (nextPage_ >= pages_.size()) {
        return std::nullopt;
    }

    const auto& path = pages_[nextPage_++];
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open PBM page: " + path.string());
    }
    if (readToken(input) != "P4") {
        throw std::runtime_error("Ghostscript output is not a raw PBM (P4) file");
    }

    int width = std::stoi(readToken(input));
    int height = std::stoi(readToken(input));
    input.get();
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("invalid PBM page dimensions");
    }

    int stride = (width + 7) / 8;
    raster_.resize(static_cast<std::size_t>(stride) * height);
    input.read(raster_.data(), static_cast<std::streamsize>(raster_.size()));
    if (input.gcount() != static_cast<std::streamsize>(raster_.size())) {
        throw std::runtime_error("truncated PBM page");
    }

    auto paper = MatchPaper(width, height);
    if (!paper && width > height) {
        RotateLandscape(raster_, width, height);
        stride = (width + 7) / 8;
        paper = MatchPaper(width, height);
    }
    if (!paper) {
        throw std::runtime_error(
            "unsupported page size from Ghostscript: "
            + std::to_string(width) + "x" + std::to_string(height)
            + " pixels at 600 dpi"
        );
    }

    setg(raster_.data(), raster_.data(), raster_.data() + raster_.size());
    return Capt::PageParams{
        .PaperSize = paper->CaptId,
        .TonerDensity = 0x1f,
        .Mode = 0,
        .Resolution = Capt::ResolutionIdx::RES_600,
        .SmoothEnable = true,
        .TonerSaving = false,
        .MarginLeft = 0,
        // captppd forces a non-zero top origin even for borderless media.
        .MarginTop = 1,
        .ImageLineSize = static_cast<std::uint16_t>(stride),
        .ImageLines = static_cast<std::uint16_t>(height),
        .PaperWidth = static_cast<std::uint16_t>(paper->Width),
        .PaperHeight = static_cast<std::uint16_t>(paper->Height),
    };
}
