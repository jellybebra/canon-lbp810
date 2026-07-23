#include "BridgeServer.hpp"
#include "WindowsUsbPrinter.hpp"
#include "WindowsService.hpp"
#include "Core/CaptPrinter.hpp"
#include "Core/StateReporter.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
    std::filesystem::path executableDirectory() {
        std::wstring buffer(32768, L'\0');
        DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );
        if (length == 0 || length >= buffer.size()) {
            throw std::runtime_error("GetModuleFileNameW failed");
        }
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }

    std::filesystem::path defaultGhostscript() {
        auto directory = executableDirectory();
        const std::vector<std::filesystem::path> candidates{
            directory / "gswin64c.exe",
            directory / ".." / ".." / "runtime" / "msys64" / "ucrt64" / "bin" / "gswin64c.exe",
            directory / ".." / ".." / ".." / "runtime" / "msys64" / "ucrt64" / "bin" / "gswin64c.exe",
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::is_regular_file(candidate)) {
                return std::filesystem::weakly_canonical(candidate);
            }
        }
        return candidates.front();
    }

    void usage(const char* executable) {
        std::cerr
            << "Canon LBP-810 bridge for Windows 11 x64\n\n"
            << "Usage:\n"
            << "  " << executable << " --probe\n"
            << "  " << executable << " --capt-status\n"
            << "  " << executable << " --clear-error\n"
            << "  " << executable << " --print-pbm page1.pbm [page2.pbm ...]\n"
            << "  " << executable << " --serve [--port 9100] [--gs path] [--spool path]\n"
            << "  " << executable << " --service [--port 9100] [--gs path] [--spool path]\n";
    }
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage(argv[0]);
            return 2;
        }

        const std::string mode = argv[1];
        if (mode == "--probe") {
            auto devices = WindowsUsbPrinter::Enumerate();
            if (devices.empty()) {
                std::cerr << "No available USB printer interfaces were found.\n";
                return 1;
            }
            for (const auto& device : devices) {
                std::wcout << L"Path: " << device.Path << L'\n';
                std::cout << "IEEE-1284 ID: " << device.DeviceId << '\n';
                std::cout << "LPT status: 0x"
                    << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<unsigned>(device.LptStatus)
                    << std::dec << "\n\n";
            }
            return 0;
        }

        if (mode == "--capt-status") {
            WindowsUsbPrinter usb(std::chrono::seconds(15));
            usb.Open();

            WindowsUsbStreambuf usbBuffer(usb);
            std::iostream stream(&usbBuffer);
            stream.exceptions(std::ios_base::failbit | std::ios_base::badbit);
            StateReporter reporter(std::clog);
            CaptPrinter printer(stream, reporter);

            const auto info = printer.GetPrinterInfo();
            const auto status = printer.GetStatus();
            std::cout
                << "CAPT controller: device=" << static_cast<unsigned>(info.DeviceId)
                << ", type=" << static_cast<unsigned>(info.Type)
                << ", version=" << static_cast<unsigned>(info.VersionMajor)
                << "." << static_cast<unsigned>(info.VersionMinor)
                << ", blockSize=" << info.BlockSize
                << ", buffers=" << info.Buffers << '\n';
            std::cout << "CAPT status: " << status << '\n';
            return 0;
        }

        if (mode == "--clear-error") {
            WindowsUsbPrinter usb(std::chrono::seconds(15));
            usb.Open();

            WindowsUsbStreambuf usbBuffer(usb);
            std::iostream stream(&usbBuffer);
            stream.exceptions(std::ios_base::failbit | std::ios_base::badbit);
            StateReporter reporter(std::clog);
            CaptPrinter printer(stream, reporter);

            printer.ReserveUnit();
            try {
                const auto before = printer.GetStatus();
                std::cout << "CAPT status before clear: " << before << '\n';
                if (before.ClearErrorNeeded()) {
                    printer.ClearError(&before);
                }
                const auto after = printer.GetStatus();
                std::cout << "CAPT status after clear: " << after << '\n';
                printer.GoOffline();
                printer.ReleaseUnit();
                if (after.Misprint()
                    || (after.Engine & Capt::EngineReadyStatus::JAM) != 0) {
                    return 1;
                }
            } catch (...) {
                try {
                    printer.GoOffline();
                    printer.ReleaseUnit();
                } catch (...) {
                }
                throw;
            }
            return 0;
        }

        if (mode == "--print-pbm") {
            if (argc < 3) {
                throw std::runtime_error("--print-pbm requires at least one PBM file");
            }
            std::vector<std::filesystem::path> pages;
            for (int index = 2; index < argc; ++index) {
                pages.emplace_back(argv[index]);
            }
            PrintPbmFiles(pages);
            return 0;
        }

        if (mode == "--serve" || mode == "--service") {
            unsigned short port = 9100;
            std::filesystem::path ghostscript = defaultGhostscript();
            std::filesystem::path spool = executableDirectory() / "spool";
            for (int index = 2; index < argc; ++index) {
                const std::string option = argv[index];
                if (option == "--port" && index + 1 < argc) {
                    const unsigned long value = std::stoul(argv[++index]);
                    if (value == 0 || value > 65535) {
                        throw std::runtime_error("invalid TCP port");
                    }
                    port = static_cast<unsigned short>(value);
                } else if (option == "--gs" && index + 1 < argc) {
                    ghostscript = argv[++index];
                } else if (option == "--spool" && index + 1 < argc) {
                    spool = argv[++index];
                } else {
                    throw std::runtime_error("unknown or incomplete option: " + option);
                }
            }
            if (mode == "--service") {
                return RunAsWindowsService(ghostscript, spool, port);
            }
            return BridgeServer(ghostscript, spool, port).Run();
        }

        usage(argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
