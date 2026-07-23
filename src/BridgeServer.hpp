#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

class BridgeServer {
public:
    BridgeServer(
        std::filesystem::path ghostscript,
        std::filesystem::path spoolDirectory,
        unsigned short port
    );

    int Run();
    void Stop() noexcept;

private:
    std::filesystem::path ghostscript_;
    std::filesystem::path spoolDirectory_;
    unsigned short port_;
    std::atomic<unsigned long long> jobCounter_ = 0;
    std::atomic<bool> stopping_ = false;
    std::atomic<std::uintptr_t> listenerSocket_ = ~std::uintptr_t{0};

    void ReceiveAndPrintJob(std::uintptr_t socket);
    std::vector<std::filesystem::path> Render(
        const std::filesystem::path& postscript,
        const std::filesystem::path& outputPrefix
    );
    void PrintPbmPages(const std::vector<std::filesystem::path>& pages);
};

void PrintPbmFiles(const std::vector<std::filesystem::path>& pages);
