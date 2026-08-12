// OS-backend interface. One implementation per platform:
//   mac/MacScrollEngine   — CGEventTap + synthesized continuous scroll (done)
//   win/WinScrollEngine   — WH_MOUSE_LL hook + SendInput            (planned)
// Linux intentionally has no backend: the knoble_hires firmware build gives
// libinput native hi-res scrolling with no host software.

#pragma once

#include <cstdint>
#include <functional>

struct ScrollEngineSettings {
    double pxPerCount = 18.0;
    double responseMs = 60.0;
    bool invert = false;
    bool reportPhases = true; // tag events with gesture phases (overscroll etc.)
    bool enabled = true;
};

struct DeviceFilter {
    uint16_t vendorId;
    uint16_t productId;
};

class ScrollEngine {
public:
    virtual ~ScrollEngine() = default;

    // Begin intercepting. Returns false if an OS permission is missing;
    // statusCallback receives human-readable state changes either way.
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual void applySettings(const ScrollEngineSettings &settings) = 0;

    // deviceConnected(true/false) fires on matched-device arrival/removal.
    std::function<void(bool)> deviceConnected;
    // permissionMissing() fires when the OS blocks interception.
    std::function<void()> permissionMissing;
    // rawCountsActive(bool): true when wheel counts come straight from the
    // device's HID reports (exact, immune to OS acceleration); false when
    // falling back to intercepted-event values (OS curve applies).
    std::function<void(bool)> rawCountsActive;
};
