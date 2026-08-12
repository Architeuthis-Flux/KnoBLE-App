#pragma once

#include "engine/ScrollEngine.h"

#include <QSettings>

// Persistent settings, stored per-user (macOS: ~/Library/Preferences/
// tech.baselinedesign.knob.plist via QSettings native format).
class Config {
public:
    Config() : settings_("BaselineDesign", "Knob") {}

    ScrollEngineSettings load() {
        ScrollEngineSettings s;
        s.pxPerCount = settings_.value("scroll/pxPerCount", s.pxPerCount).toDouble();
        s.responseMs = settings_.value("scroll/responseMs", s.responseMs).toDouble();
        s.invert = settings_.value("scroll/invert", s.invert).toBool();
        s.reportPhases = settings_.value("scroll/reportPhases", s.reportPhases).toBool();
        s.enabled = settings_.value("scroll/enabled", s.enabled).toBool();
        return s;
    }

    void save(const ScrollEngineSettings &s) {
        settings_.setValue("scroll/pxPerCount", s.pxPerCount);
        settings_.setValue("scroll/responseMs", s.responseMs);
        settings_.setValue("scroll/invert", s.invert);
        settings_.setValue("scroll/reportPhases", s.reportPhases);
        settings_.setValue("scroll/enabled", s.enabled);
    }

private:
    QSettings settings_;
};
