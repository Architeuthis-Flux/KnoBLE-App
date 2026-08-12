#pragma once

#include "engine/ScrollEngine.h"

#include <memory>
#include <vector>

// macOS backend: a session CGEventTap swallows the knob's discrete wheel
// events (matched per-event by HID sender ID) and re-posts them as
// phase-annotated continuous pixel scrolling driven by KnobScrollModel at
// 120 Hz. Everything runs on a dedicated CFRunLoop thread; the Qt side only
// touches applySettings() and the callbacks (invoked on unspecified threads —
// marshal to the GUI thread yourself).
//
// Requires the Accessibility permission (event taps). start() prompts once
// via AXIsProcessTrustedWithOptions and reports failure until granted.

class MacScrollEngine final : public ScrollEngine {
public:
    explicit MacScrollEngine(std::vector<DeviceFilter> devices);
    ~MacScrollEngine() override;

    bool start() override;
    void stop() override;
    void applySettings(const ScrollEngineSettings &settings) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
