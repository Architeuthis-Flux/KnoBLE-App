// Windows implementation of the knob's raw-HID settings channel.
//
// Plain Win32: SetupAPI enumerates HID interfaces, hid.dll qualifies the
// vendor collection (VID/PID + usage page 0xFF60 / usage 0x61 — vendor
// collections are openable on Windows; mouse/keyboard collections are not),
// and I/O is overlapped ReadFile/WriteFile on the device handle. Windows
// frames carry a leading report-ID byte (0 for us): buffers must be exactly
// Input/OutputReportByteLength long or the calls fail with
// ERROR_INVALID_PARAMETER.
//
// Threading: a reader thread blocks in overlapped ReadFile and marshals
// frames to the GUI thread with queued invokeMethod; a 2 s QTimer on the
// GUI thread rescans for the device while disconnected. Writes happen on
// the GUI thread with their own OVERLAPPED (concurrent ReadFile on the same
// handle is fine with distinct OVERLAPPED structs).

#include "app/KnobHidChannel.h"

#include <QTimer>

#include <windows.h>

#include <hidsdi.h>

#include <hidpi.h> // HidP_GetCaps / HIDP_STATUS_SUCCESS (not pulled in by hidsdi.h on all SDKs)
#include <setupapi.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr USHORT kVendorId = 0x1d50;
constexpr USHORT kProductId = 0x615e;
constexpr USAGE kUsagePage = 0xFF60; // QMK raw HID
constexpr USAGE kUsage = 0x61;
constexpr size_t kFrameSize = 32;

enum Cmd : uint8_t {
    CmdGetInfo = 0x01,
    CmdGetKey = 0x02,
    CmdSetKey = 0x03,
    CmdCommit = 0x04,
    CmdReset = 0x05,
    CmdGetPotCfg = 0x06,
    CmdSetPotCfg = 0x07,
    CmdGetPotValue = 0x08,
};

// Find the vendor collection's device path. Empty string if absent.
std::wstring findChannelPath() {
    std::wstring result;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo =
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return result;
    }

    SP_DEVICE_INTERFACE_DATA ifaceData;
    ifaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, index, &ifaceData); index++) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &needed, nullptr);
        if (needed == 0) {
            continue;
        }
        std::vector<BYTE> buffer(needed);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W); // struct size, NOT buffer
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, needed, nullptr,
                                              nullptr)) {
            continue;
        }

        HANDLE handle =
            CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        HIDD_ATTRIBUTES attrs;
        attrs.Size = sizeof(HIDD_ATTRIBUTES);
        bool match = false;
        if (HidD_GetAttributes(handle, &attrs) && attrs.VendorID == kVendorId &&
            attrs.ProductID == kProductId) {
            PHIDP_PREPARSED_DATA preparsed = nullptr;
            if (HidD_GetPreparsedData(handle, &preparsed)) {
                HIDP_CAPS caps;
                if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
                    caps.UsagePage == kUsagePage && caps.Usage == kUsage) {
                    match = true;
                }
                HidD_FreePreparsedData(preparsed);
            }
        }
        CloseHandle(handle);

        if (match) {
            result = detail->DevicePath;
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

} // namespace

struct KnobHidChannel::Impl {
    KnobHidChannel *owner = nullptr;

    HANDLE device = INVALID_HANDLE_VALUE;
    USHORT inputLen = 0;  // InputReportByteLength incl. report-ID byte
    USHORT outputLen = 0; // OutputReportByteLength incl. report-ID byte

    std::thread reader;
    HANDLE stopEvent = nullptr; // signals the reader to exit
    std::atomic<bool> open{false};

    QTimer *scanTimer = nullptr;
    uint8_t seq = 0;

    // ---- lifecycle (GUI thread) ----

    bool tryOpen() {
        const std::wstring path = findChannelPath();
        if (path.empty()) {
            return false;
        }

        device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, nullptr);
        if (device == INVALID_HANDLE_VALUE) {
            return false;
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps;
        if (!HidD_GetPreparsedData(device, &preparsed) ||
            HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) {
            if (preparsed) {
                HidD_FreePreparsedData(preparsed);
            }
            CloseHandle(device);
            device = INVALID_HANDLE_VALUE;
            return false;
        }
        inputLen = caps.InputReportByteLength;
        outputLen = caps.OutputReportByteLength;
        HidD_FreePreparsedData(preparsed);

        if (inputLen < 1 + kFrameSize || outputLen < 1 + kFrameSize) {
            CloseHandle(device);
            device = INVALID_HANDLE_VALUE;
            return false;
        }

        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        open.store(true);
        reader = std::thread([this] { readLoop(); });
        return true;
    }

    void close() {
        if (!open.exchange(false)) {
            return;
        }
        if (stopEvent) {
            SetEvent(stopEvent);
        }
        if (device != INVALID_HANDLE_VALUE) {
            CancelIoEx(device, nullptr);
        }
        if (reader.joinable()) {
            reader.join();
        }
        if (device != INVALID_HANDLE_VALUE) {
            CloseHandle(device);
            device = INVALID_HANDLE_VALUE;
        }
        if (stopEvent) {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
    }

    // ---- reader thread ----

    void readLoop() {
        std::vector<uint8_t> buffer(inputLen);
        HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        while (open.load()) {
            OVERLAPPED ov;
            std::memset(&ov, 0, sizeof(ov));
            ov.hEvent = readEvent;
            ResetEvent(readEvent);

            DWORD got = 0;
            if (!ReadFile(device, buffer.data(), (DWORD)buffer.size(), &got, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    break; // device gone
                }
                HANDLE waits[2] = {readEvent, stopEvent};
                const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (which != WAIT_OBJECT_0) {
                    CancelIoEx(device, &ov);
                    GetOverlappedResult(device, &ov, &got, TRUE);
                    break; // stop requested
                }
                if (!GetOverlappedResult(device, &ov, &got, FALSE)) {
                    break;
                }
            }
            if (got < 1 + 8) {
                continue; // too short to matter (byte 0 is the report ID)
            }
            handleFrame(buffer.data() + 1, got - 1);
        }

        CloseHandle(readEvent);
        // Tell the GUI thread the pipe dropped (unplug or error).
        QMetaObject::invokeMethod(
            owner, [o = owner] { o->deviceDropped(); }, Qt::QueuedConnection);
    }

    void handleFrame(const uint8_t *frame, size_t len) {
        if (len < 8) {
            return;
        }
        const uint8_t cmd = frame[0];
        const uint8_t status = frame[2];
        // Copy scalars now; the lambda runs later on the GUI thread.
        KnobHidChannel *o = owner;
        switch (cmd) {
        case CmdGetInfo:
            if (status == 0) {
                // NB: not named "slots" — that's a Qt keyword macro that
                // expands to nothing and breaks the declaration under MSVC.
                const int proto = frame[3], slotCount = frame[4], flags = frame[5];
                QMetaObject::invokeMethod(
                    o, [=] { emit o->infoLoaded(proto, slotCount, flags); },
                    Qt::QueuedConnection);
            }
            break;
        case CmdGetKey:
            if (status == 0) {
                const int slot = frame[3];
                const uint32_t code = (uint32_t)frame[4] | ((uint32_t)frame[5] << 8) |
                                      ((uint32_t)frame[6] << 16) | ((uint32_t)frame[7] << 24);
                QMetaObject::invokeMethod(
                    o, [=] { emit o->keyLoaded(slot, code); }, Qt::QueuedConnection);
            }
            break;
        case CmdCommit:
        case CmdReset: {
            const bool ok = status == 0;
            QMetaObject::invokeMethod(
                o, [=] { emit o->committed(ok); }, Qt::QueuedConnection);
            break;
        }
        case CmdGetPotCfg:
            if (status == 0) {
                const int role = frame[3], mx = frame[4], mn = frame[5], steps = frame[6];
                QMetaObject::invokeMethod(
                    o, [=] { emit o->potConfigLoaded(role, mx, mn, steps); },
                    Qt::QueuedConnection);
            }
            break;
        case CmdGetPotValue:
            if (status == 0) {
                const int raw = frame[3] | (frame[4] << 8);
                const int role = frame[5];
                const int semantic = (int16_t)(frame[6] | (frame[7] << 8));
                QMetaObject::invokeMethod(
                    o, [=] { emit o->potValue(raw, role, semantic); }, Qt::QueuedConnection);
            }
            break;
        default:
            break;
        }
    }

    // ---- writes (GUI thread) ----

    void send(uint8_t cmd, const uint8_t *payload, size_t payloadLen) {
        if (!open.load() || device == INVALID_HANDLE_VALUE) {
            return;
        }
        std::vector<uint8_t> buffer(outputLen, 0);
        buffer[0] = 0; // report ID
        buffer[1] = cmd;
        buffer[2] = ++seq;
        if (payload && payloadLen > 0) {
            std::memcpy(&buffer[3], payload, payloadLen < kFrameSize - 2 ? payloadLen
                                                                         : kFrameSize - 2);
        }

        OVERLAPPED ov;
        std::memset(&ov, 0, sizeof(ov));
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD written = 0;
        if (!WriteFile(device, buffer.data(), (DWORD)buffer.size(), &written, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                GetOverlappedResult(device, &ov, &written, TRUE);
            }
        }
        CloseHandle(ov.hEvent);
    }
};

KnobHidChannel::KnobHidChannel(QObject *parent) : QObject(parent), impl_(new Impl) {
    impl_->owner = this;
    impl_->scanTimer = new QTimer(this);
    impl_->scanTimer->setInterval(2000);
    connect(impl_->scanTimer, &QTimer::timeout, this, [this] {
        if (!impl_->open.load() && impl_->tryOpen()) {
            impl_->scanTimer->stop();
            emit presentChanged(true);
        }
    });
    // First scan immediately, then poll while absent.
    if (impl_->tryOpen()) {
        emit presentChanged(true);
    } else {
        impl_->scanTimer->start();
    }
}

KnobHidChannel::~KnobHidChannel() {
    impl_->close();
    delete impl_;
}

// Reader-thread exit lands here (queued): tear down and resume scanning.
void KnobHidChannel::deviceDropped() {
    if (!impl_->open.load() && impl_->device == INVALID_HANDLE_VALUE) {
        return; // already closed
    }
    impl_->close();
    emit presentChanged(false);
    impl_->scanTimer->start();
}

bool KnobHidChannel::channelPresent() const {
    return impl_->open.load();
}

void KnobHidChannel::requestKeys() {
    impl_->send(CmdGetInfo, nullptr, 0);
    for (uint8_t slot = 0; slot < kKeySlots; slot++) {
        const uint8_t payload[1] = {slot};
        impl_->send(CmdGetKey, payload, sizeof(payload));
    }
}

void KnobHidChannel::setKey(uint8_t slot, uint32_t encoded) {
    const uint8_t payload[5] = {
        slot,
        (uint8_t)(encoded & 0xFF),
        (uint8_t)((encoded >> 8) & 0xFF),
        (uint8_t)((encoded >> 16) & 0xFF),
        (uint8_t)((encoded >> 24) & 0xFF),
    };
    impl_->send(CmdSetKey, payload, sizeof(payload));
}

void KnobHidChannel::commit() {
    impl_->send(CmdCommit, nullptr, 0);
}

void KnobHidChannel::resetDefaults() {
    impl_->send(CmdReset, nullptr, 0);
}

void KnobHidChannel::requestPotConfig() {
    impl_->send(CmdGetPotCfg, nullptr, 0);
}

void KnobHidChannel::setPotConfig(uint8_t role, uint8_t speedMax, uint8_t speedMinDiv,
                                  uint8_t steps) {
    const uint8_t payload[4] = {role, speedMax, speedMinDiv, steps};
    impl_->send(CmdSetPotCfg, payload, sizeof(payload));
}

void KnobHidChannel::requestPotValue() {
    impl_->send(CmdGetPotValue, nullptr, 0);
}
