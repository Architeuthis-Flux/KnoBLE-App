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
    void onInfoLoaded(int protoVersion, int keySlots, int flags);
    void onKeyLoaded(int slot, uint32_t encoded);
    void onCommitted(bool ok);
    void onPotConfigLoaded(int role, int speedMax, int speedMinDiv, int steps);
    void onPotValue(int raw, int role, int semantic);
    void onDozeConfigLoaded(int timeoutSeconds, int pollHz);

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

    void sendDozeConfig();
    QComboBox *dozeCombo_ = nullptr;
    QSlider *dozeHzSlider_ = nullptr;
    QLabel *dozeHzValue_ = nullptr;
    int dozeTimeoutS_ = 120;
    int dozeHz_ = 4;

    void sendPotConfig();
    void updatePotSensUi();
    void populatePotRoles();
    bool hasFixedSpeed_ = false;
    QSlider *speedRangeSlider_ = nullptr;
    QLabel *speedRangeValue_ = nullptr;
    int speedRangeRow_ = -1;
    class QFormLayout *potForm_ = nullptr;
    QGroupBox *potBox_ = nullptr;
    QComboBox *potRoleCombo_ = nullptr;
    QSlider *potSensSlider_ = nullptr;
    QLabel *potSensValue_ = nullptr;
    class QProgressBar *potBar_ = nullptr;
    QLabel *potValueLabel_ = nullptr;
    class QTimer *potTimer_ = nullptr;
    int potRole_ = 0;
    int potSpeedMax_ = 4;
    int potSpeedMinDiv_ = 5;
    int potSteps_ = 32;
    int potLastSemantic_ = 0;
    int potLastRaw_ = -1;
    QLabel *connectionLabel_ = nullptr;
    QPlainTextEdit *reportView_ = nullptr;
};
