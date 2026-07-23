#include "WindowsUsbPrinter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <initguid.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <winioctl.h>
#include <usbprint.h>
#include <setupapi.h>

namespace {
    struct DeviceInfoSetCloser {
        void operator()(void* value) const noexcept {
            if (value != INVALID_HANDLE_VALUE) {
                SetupDiDestroyDeviceInfoList(static_cast<HDEVINFO>(value));
            }
        }
    };

    struct HandleCloser {
        void operator()(void* value) const noexcept {
            if (value != nullptr && value != INVALID_HANDLE_VALUE) {
                CloseHandle(static_cast<HANDLE>(value));
            }
        }
    };

    using DeviceInfoSet = std::unique_ptr<void, DeviceInfoSetCloser>;
    using UniqueHandle = std::unique_ptr<void, HandleCloser>;

    [[noreturn]] void throwLastError(const char* operation) {
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(),
            operation
        );
    }

    bool isLbp810(std::string_view deviceId) {
        std::string normalized(deviceId);
        std::ranges::transform(normalized, normalized.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return normalized.find("MDL:LBP-810") != std::string::npos
            || normalized.find("MODEL:LBP-810") != std::string::npos
            || normalized.find("DES:CANON LBP-810") != std::string::npos;
    }

    template<typename StartOperation>
    DWORD runOverlapped(
        HANDLE handle,
        std::chrono::milliseconds timeout,
        StartOperation&& start
    ) {
        UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) {
            throwLastError("CreateEventW");
        }

        OVERLAPPED operation{};
        operation.hEvent = event.get();
        if (start(operation)) {
            DWORD transferred = 0;
            if (!GetOverlappedResult(handle, &operation, &transferred, TRUE)) {
                throwLastError("GetOverlappedResult");
            }
            return transferred;
        }

        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            SetLastError(error);
            throwLastError("overlapped I/O");
        }

        DWORD wait = WaitForSingleObject(
            operation.hEvent,
            static_cast<DWORD>(std::clamp<std::int64_t>(timeout.count(), 1, MAXDWORD - 1))
        );
        if (wait == WAIT_TIMEOUT) {
            CancelIoEx(handle, &operation);
            WaitForSingleObject(operation.hEvent, INFINITE);
            throw std::runtime_error("USB operation timed out");
        }
        if (wait != WAIT_OBJECT_0) {
            throwLastError("WaitForSingleObject");
        }

        DWORD transferred = 0;
        if (!GetOverlappedResult(handle, &operation, &transferred, FALSE)) {
            throwLastError("GetOverlappedResult");
        }
        return transferred;
    }

    DWORD deviceIoControl(
        HANDLE handle,
        DWORD code,
        void* output,
        DWORD outputSize,
        std::chrono::milliseconds timeout
    ) {
        return runOverlapped(handle, timeout, [&](OVERLAPPED& operation) {
            return DeviceIoControl(
                handle,
                code,
                nullptr,
                0,
                output,
                outputSize,
                nullptr,
                &operation
            ) != FALSE;
        });
    }
}

HANDLE WindowsUsbPrinter::OpenPath(const std::wstring& path) {
    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        throwLastError("CreateFileW(GUID_DEVINTERFACE_USBPRINT)");
    }
    return handle;
}

std::string WindowsUsbPrinter::ReadDeviceId(
    HANDLE handle,
    std::chrono::milliseconds timeout
) {
    std::array<unsigned char, 4094> buffer{};
    DWORD transferred = deviceIoControl(
        handle,
        IOCTL_USBPRINT_GET_1284_ID,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        timeout
    );
    if (transferred < 3) {
        throw std::runtime_error("printer returned a truncated IEEE-1284 ID");
    }

    std::size_t declared = (static_cast<std::size_t>(buffer[0]) << 8)
        | static_cast<std::size_t>(buffer[1]);
    std::size_t available = transferred - 2;
    std::size_t length = declared >= 2 ? declared - 2 : 0;
    length = std::min(length, available);
    while (length > 0 && buffer[1 + length] == '\0') {
        --length;
    }
    return {
        reinterpret_cast<const char*>(buffer.data() + 2),
        length
    };
}

unsigned char WindowsUsbPrinter::ReadLptStatus(
    HANDLE handle,
    std::chrono::milliseconds timeout
) {
    unsigned char status = 0;
    deviceIoControl(
        handle,
        IOCTL_USBPRINT_GET_LPT_STATUS,
        &status,
        sizeof(status),
        timeout
    );
    return status;
}

std::vector<WindowsUsbPrinter::Device> WindowsUsbPrinter::Enumerate() {
    DeviceInfoSet devices(SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USBPRINT,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    ));
    if (!devices || devices.get() == INVALID_HANDLE_VALUE) {
        throwLastError("SetupDiGetClassDevsW");
    }

    std::vector<Device> result;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(
            static_cast<HDEVINFO>(devices.get()),
            nullptr,
            &GUID_DEVINTERFACE_USBPRINT,
            index,
            &interfaceData
        )) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) {
                break;
            }
            throwLastError("SetupDiEnumDeviceInterfaces");
        }

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(
            static_cast<HDEVINFO>(devices.get()),
            &interfaceData,
            nullptr,
            0,
            &required,
            nullptr
        );
        if (required == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            throwLastError("SetupDiGetDeviceInterfaceDetailW(size)");
        }

        std::vector<std::byte> detailBuffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
            detailBuffer.data()
        );
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
            static_cast<HDEVINFO>(devices.get()),
            &interfaceData,
            detail,
            required,
            nullptr,
            nullptr
        )) {
            throwLastError("SetupDiGetDeviceInterfaceDetailW");
        }

        Device device;
        device.Path = detail->DevicePath;
        try {
            UniqueHandle handle(OpenPath(device.Path));
            device.DeviceId = ReadDeviceId(handle.get(), std::chrono::seconds(5));
            device.LptStatus = ReadLptStatus(handle.get(), std::chrono::seconds(5));
            result.push_back(std::move(device));
        } catch (const std::exception&) {
            // Another print component can temporarily own a USB printer.
            // Keep enumerating so the caller gets any device that is available.
        }
    }
    return result;
}

WindowsUsbPrinter::WindowsUsbPrinter(std::chrono::milliseconds timeout)
    : timeout_(timeout) {}

WindowsUsbPrinter::~WindowsUsbPrinter() noexcept {
    Close();
}

void WindowsUsbPrinter::Open() {
    if (IsOpen()) {
        return;
    }
    auto devices = Enumerate();
    auto match = std::ranges::find_if(devices, [](const Device& device) {
        return isLbp810(device.DeviceId);
    });
    if (match == devices.end()) {
        throw std::runtime_error(
            "Canon LBP-810 was not found or is busy (expected an IEEE-1284 ID containing MDL:LBP-810)"
        );
    }
    info_ = *match;
    handle_ = OpenPath(info_.Path);
}

void WindowsUsbPrinter::Close() noexcept {
    if (IsOpen()) {
        CancelIoEx(handle_, nullptr);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

void WindowsUsbPrinter::SoftReset() {
    if (!IsOpen()) {
        throw std::logic_error("USB printer is not open");
    }
    deviceIoControl(
        handle_,
        IOCTL_USBPRINT_SOFT_RESET,
        nullptr,
        0,
        timeout_
    );
}

bool WindowsUsbPrinter::IsOpen() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE;
}

const WindowsUsbPrinter::Device& WindowsUsbPrinter::Info() const noexcept {
    return info_;
}

std::size_t WindowsUsbPrinter::Read(void* buffer, std::size_t size) {
    if (!IsOpen()) {
        throw std::logic_error("USB printer is not open");
    }
    DWORD requested = static_cast<DWORD>(std::min<std::size_t>(size, MAXDWORD));
    return runOverlapped(handle_, timeout_, [&](OVERLAPPED& operation) {
        return ReadFile(handle_, buffer, requested, nullptr, &operation) != FALSE;
    });
}

std::size_t WindowsUsbPrinter::Write(const void* buffer, std::size_t size) {
    if (!IsOpen()) {
        throw std::logic_error("USB printer is not open");
    }
    DWORD requested = static_cast<DWORD>(std::min<std::size_t>(size, MAXDWORD));
    return runOverlapped(handle_, timeout_, [&](OVERLAPPED& operation) {
        return WriteFile(handle_, buffer, requested, nullptr, &operation) != FALSE;
    });
}

WindowsUsbStreambuf::WindowsUsbStreambuf(
    WindowsUsbPrinter& printer,
    std::size_t writeBufferSize
) : printer_(printer), readBuffer_(4096), writeBuffer_(writeBufferSize) {
    setp(writeBuffer_.data(), writeBuffer_.data() + writeBuffer_.size() - 1);
}

WindowsUsbStreambuf::int_type WindowsUsbStreambuf::overflow(int_type value) {
    if (!traits_type::eq_int_type(value, traits_type::eof())) {
        *pptr() = traits_type::to_char_type(value);
        pbump(1);
    }
    return sync() == 0 ? traits_type::not_eof(value) : traits_type::eof();
}

WindowsUsbStreambuf::int_type WindowsUsbStreambuf::underflow() {
    if (gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }
    std::size_t read = printer_.Read(readBuffer_.data(), readBuffer_.size());
    if (read == 0) {
        return traits_type::eof();
    }
    setg(
        readBuffer_.data(),
        readBuffer_.data(),
        readBuffer_.data() + read
    );
    return traits_type::to_int_type(*gptr());
}

int WindowsUsbStreambuf::sync() {
    std::ptrdiff_t remaining = pptr() - pbase();
    const char* cursor = pbase();
    while (remaining > 0) {
        std::size_t written = printer_.Write(
            cursor,
            static_cast<std::size_t>(remaining)
        );
        if (written == 0) {
            return -1;
        }
        cursor += written;
        remaining -= static_cast<std::ptrdiff_t>(written);
    }
    setp(writeBuffer_.data(), writeBuffer_.data() + writeBuffer_.size() - 1);
    return 0;
}
