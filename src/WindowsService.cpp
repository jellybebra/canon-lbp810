#include "WindowsService.hpp"

#include "BridgeServer.hpp"
#include "Core/Log.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <windows.h>

namespace {
    constexpr wchar_t ServiceName[] = L"CanonLBP810Bridge";

    SERVICE_STATUS_HANDLE statusHandle = nullptr;
    SERVICE_STATUS serviceStatus{};
    std::unique_ptr<BridgeServer> server;
    std::filesystem::path configuredGhostscript;
    std::filesystem::path configuredSpool;
    unsigned short configuredPort = 9100;

    void reportStatus(DWORD state, DWORD error = NO_ERROR, DWORD hint = 0) {
        serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        serviceStatus.dwCurrentState = state;
        serviceStatus.dwWin32ExitCode = error;
        serviceStatus.dwWaitHint = hint;
        serviceStatus.dwControlsAccepted =
            state == SERVICE_START_PENDING
                ? 0
                : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        SetServiceStatus(statusHandle, &serviceStatus);
    }

    DWORD WINAPI controlHandler(
        DWORD control,
        DWORD,
        void*,
        void*
    ) {
        if (control == SERVICE_CONTROL_STOP
            || control == SERVICE_CONTROL_SHUTDOWN) {
            reportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            if (server) {
                server->Stop();
            }
        }
        return NO_ERROR;
    }

    void WINAPI serviceMain(DWORD, wchar_t**) {
        statusHandle = RegisterServiceCtrlHandlerExW(
            ServiceName,
            controlHandler,
            nullptr
        );
        if (!statusHandle) {
            return;
        }
        reportStatus(SERVICE_START_PENDING, NO_ERROR, 5000);

        DWORD error = NO_ERROR;
        std::ofstream log;
        std::streambuf* previousClog = nullptr;
        try {
            std::filesystem::create_directories(configuredSpool);
            log.open(configuredSpool / "bridge.log", std::ios::app);
            if (!log) {
                throw std::runtime_error("failed to open service log");
            }
            previousClog = std::clog.rdbuf(log.rdbuf());
            Log::SetLogStream(log);
            Log::Info() << "Canon LBP-810 bridge service starting";

            server = std::make_unique<BridgeServer>(
                configuredGhostscript,
                configuredSpool,
                configuredPort
            );
            reportStatus(SERVICE_RUNNING);
            server->Run();
        } catch (const std::exception& exception) {
            if (log) {
                Log::Critical() << "Service failed: " << exception.what();
            }
            error = ERROR_SERVICE_SPECIFIC_ERROR;
        } catch (...) {
            if (log) {
                Log::Critical() << "Service failed with an unknown exception";
            }
            error = ERROR_SERVICE_SPECIFIC_ERROR;
        }
        server.reset();
        if (log) {
            Log::Info() << "Canon LBP-810 bridge service stopped";
            Log::SetLogStream(std::clog);
        }
        if (previousClog) {
            std::clog.rdbuf(previousClog);
        }
        reportStatus(SERVICE_STOPPED, error);
    }
}

int RunAsWindowsService(
    std::filesystem::path ghostscript,
    std::filesystem::path spoolDirectory,
    unsigned short port
) {
    configuredGhostscript = std::move(ghostscript);
    configuredSpool = std::move(spoolDirectory);
    configuredPort = port;

    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<wchar_t*>(ServiceName), serviceMain},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(),
            "StartServiceCtrlDispatcherW"
        );
    }
    return 0;
}
