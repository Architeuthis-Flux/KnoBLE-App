#pragma once

#include "engine/ScrollEngine.h"

#include <QWidget>
#include <functional>

class QSlider;
class QLabel;
class QCheckBox;
class QPlainTextEdit;

// Live-editing settings panel: every change applies to the engine instantly
// (scroll with the knob while dragging a slider to feel it).
//
// Tabs: Scrolling (host-side feel + a tester scroll area), Device (status,
// full settings report, and the firmware-side values — read-only until the
// raw-HID settings channel lands in v0.2).
class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWindow(ScrollEngineSettings initial, QWidget *parent = nullptr);

    std::function<void(const ScrollEngineSettings &)> onChanged;

public slots:
    void setDeviceConnected(bool connected);
    void setRawCountsActive(bool active);

private:
    QWidget *buildScrollingTab();
    QWidget *buildDeviceTab();
    void emitChanged();
    void checkLinearMouseConflict();
    void refreshReport();
    QString buildReport() const;

    ScrollEngineSettings current_;
    bool deviceConnected_ = false;
    bool rawCounts_ = false;

    QSlider *speedSlider_ = nullptr;
    QLabel *speedValue_ = nullptr;
    QSlider *responseSlider_ = nullptr;
    QLabel *responseValue_ = nullptr;
    QCheckBox *invertBox_ = nullptr;
    QCheckBox *phasesBox_ = nullptr;
    QLabel *conflictLabel_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QPlainTextEdit *reportView_ = nullptr;
};
