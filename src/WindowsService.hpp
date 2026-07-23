#pragma once

#include <filesystem>

int RunAsWindowsService(
    std::filesystem::path ghostscript,
    std::filesystem::path spoolDirectory,
    unsigned short port
);
