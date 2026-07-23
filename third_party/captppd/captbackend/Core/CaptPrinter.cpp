#include "CaptPrinter.hpp"
#include "StatusMessage.hpp"
#include "Log.hpp"
#include <cassert>
#include <chrono>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <libcapt/Protocol/Protocol.hpp>
#include <libcapt/Utility/Crop.hpp>
#include <libcapt/Utility/CropStreambuf.hpp>
#include <libcapt/Compression/ScoaStreambuf.hpp>

using namespace std::literals::chrono_literals;

static inline Capt::Utility::CropStreambuf crop(RasterStreambuf& rasterStr, Capt::PageParams& params) noexcept {
    uint16_t lineSize = Capt::Utility::CropLineSize(params.ImageLineSize, params.PaperWidth);
    uint16_t lines = Capt::Utility::CropLinesCount(params.ImageLines, params.PaperHeight);
    Capt::Utility::CropStreambuf cropStr(rasterStr, params.ImageLineSize, params.ImageLines, lineSize, lines);
    Log::Debug() << "Cropping raster from " << params.ImageLineSize << 'x' << params.ImageLines
        << " to " << lineSize << 'x' << lines;
    params.ImageLineSize = lineSize;
    params.ImageLines = lines;
    return cropStr;
}

CaptPrinter::CaptPrinter(std::iostream& stream, StateReporter& reporter) noexcept
    : Capt::BasicCaptPrinter<StopTokenType>(stream), reporter(reporter) {}

Capt::ExtendedStatus CaptPrinter::GetStatus() {
    Capt::ExtendedStatus status = this->Capt::BasicCaptPrinter<StopTokenType>::GetStatus();
    this->reporter.Update(status);
    return status;
}

bool CaptPrinter::WriteVideoData(
    StopTokenType stopToken,
    const Capt::PageParams& params,
    std::streambuf& videoStream,
    std::size_t blockSize
) {
    using namespace std::literals::chrono_literals;

    if (blockSize == 0) {
        blockSize = this->GetPrinterInfo().BlockSize;
    }
    Capt::Protocol::IC_BEGIN_PAGE(this->stream, params);
    Capt::Protocol::IC_BEGIN_DATA(this->stream);

    std::vector<std::uint8_t> buffer(blockSize);
    std::uint64_t total = 0;
    unsigned blocks = 0;
    while (true) {
        const std::streamsize read = videoStream.sgetn(
            reinterpret_cast<char*>(buffer.data()),
            buffer.size()
        );
        if (read <= 0) {
            break;
        }

        const auto busyStarted = std::chrono::steady_clock::now();
        auto nextBusyLog = busyStarted + 5s;
        while (!stopToken.stop_requested()) {
            const Capt::BasicStatus status =
                Capt::Protocol::PCR_GET_BASIC_STATUS(this->stream);
            if ((status & Capt::BasicStatus::NOT_READY) != 0) {
                Log::Warning() << "Printer became not ready while receiving video data";
                return false;
            }
            if ((status & Capt::BasicStatus::IM_DATA_BUSY) == 0) {
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextBusyLog) {
                const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                    now - busyStarted
                ).count();
                Log::Info()
                    << "Printer image buffer busy for " << seconds
                    << " s before block " << (blocks + 1)
                    << " (basic status=0x" << std::hex
                    << static_cast<unsigned>(status) << std::dec << ')';
                nextBusyLog = now + 5s;
            }
            if (now - busyStarted >= 60s) {
                throw std::runtime_error(
                    "printer image buffer remained busy for 60 seconds"
                );
            }
            std::this_thread::sleep_for(25ms);
        }
        if (stopToken.stop_requested()) {
            return false;
        }

        Capt::Protocol::IC_VIDEO_DATA(
            this->stream,
            {buffer.data(), static_cast<std::size_t>(read)}
        );
        total += static_cast<std::uint64_t>(read);
        ++blocks;
        if (blocks == 1 || blocks % 8 == 0) {
            Log::Debug()
                << "Sent CAPT video block " << blocks
                << " (" << total << " compressed bytes)";
        }
    }
    Capt::Protocol::IC_END_PAGE(this->stream);
    Log::Info()
        << "Sent " << total << " compressed CAPT bytes in "
        << blocks << " block(s)";
    return true;
}

Capt::ExtendedStatus CaptPrinter::WaitReady(StopTokenType stopToken) {
    Capt::ExtendedStatus status = this->GetStatus();
    while (!stopToken.stop_requested() && !(status.Ready() && status.PaperAvailableBits != 0)) {
        if (status.ClearErrorNeeded()) {
            Log::Debug() << "Calling ClearError()";
            Log::Debug() << "Status is " << status;
            this->ClearError(&status);
        }
        Log::Info() << "Stopped (" << StatusMessage(status) << ')';
        std::this_thread::sleep_for(1s);
        status = this->GetStatus();
    }
    return status;
}

void CaptPrinter::PrepareBeforePrint(StopTokenType stopToken, unsigned page) {
    while (true) {
        Capt::ExtendedStatus status = this->WaitReady(stopToken);
        if (stopToken.stop_requested()) {
            return;
        }
        assert(status.Ready());
        if (!status.Online() || status.Start != page) {
            if (!this->GoOnline(page)) {
                Log::Warning() << "GoOnline failed, retrying...";
                std::this_thread::sleep_for(1s);
                continue;
            }
        }
        break;
    }
}

// Has value if error
std::optional<Capt::ExtendedStatus> CaptPrinter::WritePage(StopTokenType stopToken, Capt::Utility::BufferedPage& page, Capt::Utility::BufferedPage* prev) {
    Capt::ReprintStatus reprint = Capt::ReprintStatus::None;
    while (!stopToken.stop_requested()) {
        Capt::Utility::BufferedPage& p = (prev && reprint == Capt::ReprintStatus::Prev) ? *prev : page;
        p.pubseekpos(0);
        this->PrepareBeforePrint(stopToken, p.PageNumber);
        if (stopToken.stop_requested()) {
            return std::nullopt;
        }
        if (reprint != Capt::ReprintStatus::None) {
            Log::Info() << "Retrying page " << (p.PageNumber + 1);
        } else {
            Log::Info() << "Writing page " << (p.PageNumber + 1);
        }
        if (this->WriteVideoData(stopToken, p.Params, p)) {
            if (prev && reprint == Capt::ReprintStatus::Prev) {
                reprint = Capt::ReprintStatus::None;
                continue;
            }
            break;
        }
        auto status = this->WaitPrintEnd(stopToken);
        if (!status) {
            return std::nullopt;
        }
        if (status->VideoDataError() || status->FatalError()) {
            return status;
        }
        reprint = status->GetReprintStatus();
        assert(!status->Ready());
        std::this_thread::sleep_for(1s);
    }
    return std::nullopt;
}

// Has value if error
std::optional<Capt::ExtendedStatus> CaptPrinter::WaitLastPage(StopTokenType stopToken, Capt::Utility::BufferedPage& page) {
    const auto started = std::chrono::steady_clock::now();
    auto nextLog = started + 5s;
    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(1s);
        Capt::ExtendedStatus status = this->GetStatus();
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextLog) {
            Log::Info() << "Waiting for physical page completion: " << status;
            nextLog = now + 5s;
        }
        if (now - started >= 120s) {
            throw std::runtime_error(
                "printer did not report physical page completion within 120 seconds"
            );
        }
        if (status.IsPrinting()) {
            continue;
        }
        if (status.VideoDataError()
            || status.FatalError()
            || status.Misprint()
            || (status.Engine & Capt::EngineReadyStatus::JAM) != 0) {
            return status;
        } else if (status.GetReprintStatus() == Capt::ReprintStatus::None) {
            break;
        }
        auto res = this->WritePage(stopToken, page, nullptr);
        if (res.has_value()) {
            return *res;
        }
    }
    return std::nullopt;
}

bool CaptPrinter::Print(StopTokenType stopToken, RasterStreambuf& rasterStr) {
    unsigned page = 0;
    Capt::Utility::BufferedPage prevPage;
    Capt::Compression::ScoaStreambuf ss;
    while (!stopToken.stop_requested()) {
        std::optional<Capt::PageParams> params = rasterStr.NextPage();
        if (!params) {
            break;
        }
        Capt::Utility::CropStreambuf cropStr = crop(rasterStr, *params);
        ss.Reset(cropStr, params->ImageLineSize, params->ImageLines);
        Capt::Utility::BufferedPage currPage(page, *params, &ss);
        reporter.Page(page + 1);
        Log::Debug() << "Writing page params: ImageSize=" << static_cast<int>(params->ImageLineSize)
            << 'x' << static_cast<int>(params->ImageLines)
            << " PaperSize=" << static_cast<int>(params->PaperWidth) << 'x' << static_cast<int>(params->PaperHeight)
            << " (" << static_cast<int>(params->PaperSize)
            << ") MarginLeft=" << static_cast<int>(params->MarginLeft) << " MarginTop=" << static_cast<int>(params->MarginTop)
            << " TonerDensity=" << static_cast<int>(params->TonerDensity) << " Mode=" << static_cast<int>(params->Mode);

        auto res = this->WritePage(stopToken, currPage, page == 0 ? nullptr : &prevPage);
        if (res.has_value()) {
            Log::Debug() << "WritePage failed: " << *res;
            Log::Critical() << "Failed to write page (" << StatusMessage(*res) << ')';
            return false;
        }
        prevPage = std::move(currPage);
        page++;
    }

    Log::Info() << "Waiting for last page...";
    if (page != 0) {
        auto res = this->WaitLastPage(stopToken, prevPage);
        if (res.has_value()) {
            Log::Debug() << "WaitLastPage failed: " << *res;
            Log::Critical() << "Failed to write page (" << StatusMessage(*res) << ')';
            return false;
        }
    }
    Capt::ExtendedStatus status = this->GetStatus();
    Log::Debug() << "Status after CaptPrinter::Print(): " << status;
    return true;
}

bool CaptPrinter::Clean(StopTokenType stopToken) {
    while (!stopToken.stop_requested()) {
        this->PrepareBeforePrint(stopToken, 0);
        std::this_thread::sleep_for(1s); // Manual slot delay
        this->Cleaning();
        Log::Info() << "Cleaning...";
        std::this_thread::sleep_for(2s);

        Capt::ExtendedStatus status = this->GetStatus();
        if (status.FatalError()) {
            Log::Debug() << "Clean failed: " << status;
            Log::Critical() << "Unknown fatal error";
            return false;
        }
        if ((status.Engine & Capt::EngineReadyStatus::CLEANING) == 0) {
            Log::Warning() << "Cleaning failed (" << StatusMessage(status) << ')';
            continue;
        }
        this->WaitPrintEnd(stopToken);
        break;
    }
    Capt::ExtendedStatus status = this->GetStatus();
    Log::Debug() << "Status after CaptPrinter::Clean(): " << status;
    return true;
}
