#pragma once

#include "engine/ScrollEngine.h"

#include <QWidget>
#include <functional>

class QSlider;
class QLabel;
class QCheckBox;

// Live-editing settings panel: every change applies to the engine instantly
// (scroll with the knob while dragging a slider to feel it).
class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWindow(ScrollEngineSettings initial, QWidget *parent = nullptr);

    std::function<void(const ScrollEngineSettings &)> onChanged;

private:
    void emitChanged();
    void checkLinearMouseConflict();

    ScrollEngineSettings current_;
    QSlider *speedSlider_ = nullptr;
    QLabel *speedValue_ = nullptr;
    QSlider *responseSlider_ = nullptr;
    QLabel *responseValue_ = nullptr;
    QCheckBox *invertBox_ = nullptr;
    QCheckBox *phasesBox_ = nullptr;
    QLabel *conflictLabel_ = nullptr;
};
