#pragma once

#include "engine/ScrollEngine.h"

#include <QWidget>
#include <functional>

class QSlider;
class QLabel;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QPlainTextEdit;
class QPushButton;
class KnobHidChannel;

// Settings panel. Tabs:
//   Scrolling — live host-side feel controls + a tester scroll canvas
//   Keys      — remap the knob's key slots over the raw-HID channel (USB)
//   Device    — setup status, full settings report
// Every scroll change applies instantly; key changes stage on the device
// instantly (SET) and persist with "Save to knob" (COMMIT).
class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    SettingsWindow(ScrollEngineSettings initial, KnobHidChannel *channel,
                   QWidget *parent = nullptr);

    std::function<void(const ScrollEngineSettings &)> onChanged;

public slots:
    void setDeviceConnected(bool connected);
    void setRawCountsActive(bool active);

private slots:
    void onChannelPresent(bool present);
    void onKeyLoaded(int slot, uint32_t encoded);
    void onCommitted(bool ok);

private:
    QWidget *buildScrollingTab();
    QWidget *buildKeysTab();
    QWidget *buildDeviceTab();
    void emitChanged();
    void checkLinearMouseConflict();
    void refreshReport();
    void refreshBanner();
    QString buildReport() const;

    ScrollEngineSettings current_;
    KnobHidChannel *channel_ = nullptr;
    bool deviceConnected_ = false;
    bool rawCounts_ = false;
    uint32_t keyCodes_[3] = {0, 0, 0};
    bool keysLoaded_ = false;

    QLabel *banner_ = nullptr;
    QSlider *speedSlider_ = nullptr;
    QLabel *speedValue_ = nullptr;
    QSlider *responseSlider_ = nullptr;
    QLabel *responseValue_ = nullptr;
    QCheckBox *invertBox_ = nullptr;
    QCheckBox *phasesBox_ = nullptr;
    QLabel *conflictLabel_ = nullptr;
    QGroupBox *keysBox_ = nullptr;
    QComboBox *keyCombo_[3] = {nullptr, nullptr, nullptr};
    QLabel *keysHint_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QLabel *keysStatus_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QPlainTextEdit *reportView_ = nullptr;
};
