#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

#include <windows.h>

class WindowsUsbPrinter {
public:
    struct Device {
        std::wstring Path;
        std::string DeviceId;
        unsigned char LptStatus = 0;
    };

    static std::vector<Device> Enumerate();

    explicit WindowsUsbPrinter(std::chrono::milliseconds timeout = std::chrono::seconds(10));
    ~WindowsUsbPrinter() noexcept;

    WindowsUsbPrinter(const WindowsUsbPrinter&) = delete;
    WindowsUsbPrinter& operator=(const WindowsUsbPrinter&) = delete;

    void Open();
    void Close() noexcept;
    void SoftReset();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] const Device& Info() const noexcept;

    std::size_t Read(void* buffer, std::size_t size);
    std::size_t Write(const void* buffer, std::size_t size);

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    Device info_;
    std::chrono::milliseconds timeout_;

    static HANDLE OpenPath(const std::wstring& path);
    static std::string ReadDeviceId(HANDLE handle, std::chrono::milliseconds timeout);
    static unsigned char ReadLptStatus(HANDLE handle, std::chrono::milliseconds timeout);
};

class WindowsUsbStreambuf final : public std::streambuf {
public:
    explicit WindowsUsbStreambuf(
        WindowsUsbPrinter& printer,
        std::size_t writeBufferSize = 64 * 1024
    );

protected:
    int_type overflow(int_type value) override;
    int_type underflow() override;
    int sync() override;

private:
    WindowsUsbPrinter& printer_;
    std::vector<char> readBuffer_;
    std::vector<char> writeBuffer_;
};
