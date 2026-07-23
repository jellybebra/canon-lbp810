#include "BridgeServer.hpp"

#include "PbmRaster.hpp"
#include "WindowsUsbPrinter.hpp"
#include "Core/CaptPrinter.hpp"
#include "Core/Log.hpp"
#include "Core/StateReporter.hpp"
#include <libcapt/Protocol/Protocol.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {
    struct Winsock {
        Winsock() {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                throw std::runtime_error("WSAStartup failed");
            }
        }
        ~Winsock() {
            WSACleanup();
        }
    };

    struct SocketCloser {
        void operator()(SOCKET* value) const noexcept {
            if (value != nullptr && *value != INVALID_SOCKET) {
                closesocket(*value);
            }
            delete value;
        }
    };

    using UniqueSocket = std::unique_ptr<SOCKET, SocketCloser>;

    [[noreturn]] void throwSocketError(const char* operation) {
        throw std::system_error(
            WSAGetLastError(),
            std::system_category(),
            operation
        );
    }

    std::wstring quoteText(std::wstring text) {
        std::wstring result = L"\"";
        unsigned backslashes = 0;
        for (wchar_t c : text) {
            if (c == L'\\') {
                ++backslashes;
            } else if (c == L'"') {
                result.append(backslashes * 2 + 1, L'\\');
                result.push_back(L'"');
                backslashes = 0;
            } else {
                result.append(backslashes, L'\\');
                backslashes = 0;
                result.push_back(c);
            }
        }
        result.append(backslashes * 2, L'\\');
        result.push_back(L'"');
        return result;
    }

    std::wstring quoteNative(const std::filesystem::path& value) {
        return quoteText(value.wstring());
    }

    std::wstring quoteGhostscriptPath(const std::filesystem::path& value) {
        return quoteText(value.generic_wstring());
    }

    std::string timestamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_s(&local, &time);
        std::ostringstream value;
        value << std::put_time(&local, "%Y%m%d-%H%M%S");
        return value.str();
    }
}

void PrintPbmFiles(const std::vector<std::filesystem::path>& pages) {
    if (pages.empty()) {
        throw std::runtime_error("no PBM pages to print");
    }

    WindowsUsbPrinter usb(std::chrono::seconds(15));
    usb.Open();
    Log::Info() << "Opened " << usb.Info().DeviceId;
    usb.SoftReset();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    WindowsUsbStreambuf usbBuffer(usb);
    std::iostream printerStream(&usbBuffer);
    printerStream.exceptions(std::ios_base::failbit | std::ios_base::badbit);

    StateReporter reporter(std::clog);
    CaptPrinter printer(printerStream, reporter);
    PbmRaster raster(pages);

    printer.ReserveUnit();
    try {
        const auto initialStatus = printer.GetStatus();
        if (initialStatus.ClearErrorNeeded()) {
            Log::Info() << "Clearing recoverable CAPT error before printing";
            printer.ClearError(&initialStatus);
        }
        const auto discardResult = Capt::Protocol::PCR_DISCARD_DATA(printerStream);
        if (discardResult != 0) {
            throw std::runtime_error(
                "CAPT discard-data command failed with code "
                + std::to_string(discardResult)
            );
        }
        Log::Debug() << "Cleared any incomplete CAPT image data from the printer";
        if (!printer.Print({}, raster)) {
            throw std::runtime_error("CAPT print operation failed");
        }
        printer.GoOffline();
        printer.ReleaseUnit();
    } catch (...) {
        try {
            printer.GoOffline();
            printer.ReleaseUnit();
        } catch (...) {
        }
        throw;
    }
}

BridgeServer::BridgeServer(
    std::filesystem::path ghostscript,
    std::filesystem::path spoolDirectory,
    unsigned short port
) : ghostscript_(std::move(ghostscript)),
    spoolDirectory_(std::move(spoolDirectory)),
    port_(port) {}

void BridgeServer::Stop() noexcept {
    stopping_.store(true);
    const auto value = listenerSocket_.exchange(~std::uintptr_t{0});
    if (value != ~std::uintptr_t{0}) {
        const SOCKET listener = static_cast<SOCKET>(value);
        shutdown(listener, SD_BOTH);
        closesocket(listener);
    }
}

std::vector<std::filesystem::path> BridgeServer::Render(
    const std::filesystem::path& postscript,
    const std::filesystem::path& outputPrefix
) {
    std::filesystem::path outputPattern = outputPrefix;
    outputPattern += "-%04d.pbm";

    std::wstring command = quoteNative(ghostscript_)
        + L" -dSAFER -dBATCH -dNOPAUSE"
        + L" -sDEVICE=pbmraw -r600"
        + L" -dTextAlphaBits=1 -dGraphicsAlphaBits=1"
        + L" -sOutputFile=" + quoteGhostscriptPath(outputPattern)
        + L" " + quoteGhostscriptPath(postscript);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(
        ghostscript_.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        spoolDirectory_.c_str(),
        &startup,
        &process
    )) {
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(),
            "CreateProcessW(Ghostscript)"
        );
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (exitCode != 0) {
        throw std::runtime_error(
            "Ghostscript failed with exit code " + std::to_string(exitCode)
        );
    }

    std::vector<std::filesystem::path> pages;
    const std::string prefix = outputPrefix.filename().string() + "-";
    for (const auto& entry : std::filesystem::directory_iterator(spoolDirectory_)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.starts_with(prefix) && entry.path().extension() == ".pbm") {
            pages.push_back(entry.path());
        }
    }
    std::ranges::sort(pages);
    if (pages.empty()) {
        throw std::runtime_error("Ghostscript produced no PBM pages");
    }
    return pages;
}

void BridgeServer::PrintPbmPages(
    const std::vector<std::filesystem::path>& pages
) {
    PrintPbmFiles(pages);
}

void BridgeServer::ReceiveAndPrintJob(std::uintptr_t socketValue) {
    UniqueSocket socket(new SOCKET(static_cast<SOCKET>(socketValue)));
    const auto id = jobCounter_.fetch_add(1) + 1;
    const std::string base = "job-" + timestamp() + "-" + std::to_string(id);
    const auto rawPath = spoolDirectory_ / (base + ".raw");
    const auto psPath = spoolDirectory_ / (base + ".ps");
    const auto outputPrefix = spoolDirectory_ / base;

    std::ofstream raw(rawPath, std::ios::binary);
    if (!raw) {
        throw std::runtime_error("failed to create spool file");
    }
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    while (true) {
        int received = recv(
            *socket,
            buffer.data(),
            static_cast<int>(buffer.size()),
            0
        );
        if (received == 0) {
            break;
        }
        if (received == SOCKET_ERROR) {
            throwSocketError("recv");
        }
        total += static_cast<unsigned>(received);
        if (total > 512ull * 1024 * 1024) {
            throw std::runtime_error("print job exceeds 512 MiB");
        }
        raw.write(buffer.data(), received);
    }
    raw.close();
    if (total == 0) {
        std::filesystem::remove(rawPath);
        return;
    }

    std::ifstream source(rawPath, std::ios::binary);
    std::string prefix(128 * 1024, '\0');
    source.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<std::size_t>(source.gcount()));
    std::size_t marker = prefix.find("%!PS");
    if (marker == std::string::npos) {
        throw std::runtime_error(
            "RAW port job does not contain a PostScript header; verify that the queue uses Microsoft PS Class Driver"
        );
    }
    source.clear();
    source.seekg(static_cast<std::streamoff>(marker));
    std::ofstream postscript(psPath, std::ios::binary);
    postscript << source.rdbuf();
    postscript.close();
    source.close();

    Log::Info() << "Rendering job " << id << " (" << total << " bytes)";
    auto pages = Render(psPath, outputPrefix);
    Log::Info() << "Printing job " << id << " (" << pages.size() << " page(s))";
    PrintPbmPages(pages);
    Log::Info() << "Completed job " << id;

    std::filesystem::remove(rawPath);
    std::filesystem::remove(psPath);
    for (const auto& page : pages) {
        std::filesystem::remove(page);
    }
}

int BridgeServer::Run() {
    if (!std::filesystem::is_regular_file(ghostscript_)) {
        throw std::runtime_error(
            "Ghostscript executable not found: " + ghostscript_.string()
        );
    }
    std::filesystem::create_directories(spoolDirectory_);
    Winsock winsock;

    UniqueSocket listener(new SOCKET(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)));
    if (*listener == INVALID_SOCKET) {
        throwSocketError("socket");
    }

    BOOL exclusive = TRUE;
    setsockopt(
        *listener,
        SOL_SOCKET,
        SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive),
        sizeof(exclusive)
    );

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port_);
    if (bind(
        *listener,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    ) == SOCKET_ERROR) {
        throwSocketError("bind");
    }
    if (listen(*listener, SOMAXCONN) == SOCKET_ERROR) {
        throwSocketError("listen");
    }
    listenerSocket_.store(static_cast<std::uintptr_t>(*listener));

    Log::Info() << "LBP-810 bridge listening on 127.0.0.1:" << port_;
    while (!stopping_.load()) {
        SOCKET client = accept(*listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (stopping_.load()) {
                listenerSocket_.store(~std::uintptr_t{0});
                *listener = INVALID_SOCKET;
                return 0;
            }
            throwSocketError("accept");
        }
        try {
            ReceiveAndPrintJob(static_cast<std::uintptr_t>(client));
        } catch (const std::exception& error) {
            Log::Critical() << "Print job failed: " << error.what();
        }
    }
    listenerSocket_.store(~std::uintptr_t{0});
    return 0;
}
