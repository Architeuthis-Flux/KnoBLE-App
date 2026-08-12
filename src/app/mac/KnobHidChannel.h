#pragma once

#include <QObject>
#include <cstdint>

// Client for the knob's raw-HID settings channel (QMK-shaped: usage page
// 0xFF60, usage 0x61, 32-byte frames, USB only for now). Runs on the Qt main
// thread — Qt's macOS event dispatcher services the main CFRunLoop, so
// IOHIDManager callbacks land here directly.
//
// Uses the same Input Monitoring grant as the scroll engine.
class KnobHidChannel : public QObject {
    Q_OBJECT

public:
    static constexpr int kKeySlots = 3;

    explicit KnobHidChannel(QObject *parent = nullptr);
    ~KnobHidChannel() override;

    bool channelPresent() const;

    // Fire-and-forget commands; results arrive via the signals.
    void requestKeys();                            // GET_INFO + GET x slots
    void setKey(uint8_t slot, uint32_t encoded);   // stage in RAM on device
    void commit();                                 // persist to flash
    void resetDefaults();                          // factory keycodes + persist
    void requestPotConfig();
    // Per-role sensitivity: only the selected role's dial is updated.
    void setPotConfig(uint8_t role, uint8_t speedMax, uint8_t speedMinDiv, uint8_t steps);
    void requestPotValue(); // poll for the live slider position

signals:
    void presentChanged(bool present);
    void infoLoaded(int protoVersion, int keySlots, int flags); // flags bit0: fixed speed slider
    void keyLoaded(int slot, uint32_t encoded);
    void committed(bool ok);
    void potConfigLoaded(int role, int speedMax, int speedMinDiv, int steps);
    void potValue(int raw, int role, int semantic);

private:
    struct Impl;
    Impl *impl_;
};
