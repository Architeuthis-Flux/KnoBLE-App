#include "app/KnobHidChannel.h"

#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDManager.h>

#import <Foundation/Foundation.h>

namespace {
constexpr uint16_t kVendorId = 0x1d50;
constexpr uint16_t kProductId = 0x615e;
constexpr uint32_t kUsagePage = 0xFF60; // QMK raw HID
constexpr uint32_t kUsage = 0x61;
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
} // namespace

struct KnobHidChannel::Impl {
    KnobHidChannel *owner = nullptr;
    IOHIDManagerRef manager = nullptr;
    IOHIDDeviceRef device = nullptr; // borrowed from manager's set
    uint8_t reportBuf[kFrameSize] = {};
    uint8_t seq = 0;

    void onDeviceMatched(IOHIDDeviceRef dev) {
        if (device) {
            return; // first matching channel wins
        }
        device = dev;
        IOHIDDeviceRegisterInputReportCallback(
            dev, reportBuf, sizeof(reportBuf),
            [](void *ctx, IOReturn, void *, IOHIDReportType, uint32_t, uint8_t *report,
               CFIndex length) {
                static_cast<Impl *>(ctx)->onReport(report, (size_t)length);
            },
            this);
        emitPresent(true);
    }

    void onDeviceRemoved(IOHIDDeviceRef dev) {
        if (dev == device) {
            device = nullptr;
            emitPresent(false);
        }
    }

    void emitPresent(bool present) {
        emit owner->presentChanged(present);
    }

    void onReport(const uint8_t *frame, size_t len) {
        if (len < 8) {
            return;
        }
        const uint8_t cmd = frame[0];
        const uint8_t status = frame[2];
        switch (cmd) {
        case CmdGetInfo:
            if (status == 0) {
                emit owner->infoLoaded(frame[3], frame[4], frame[5]);
            }
            break;
        case CmdGetKey:
            if (status == 0) {
                const int slot = frame[3];
                const uint32_t code = (uint32_t)frame[4] | ((uint32_t)frame[5] << 8) |
                                      ((uint32_t)frame[6] << 16) | ((uint32_t)frame[7] << 24);
                emit owner->keyLoaded(slot, code);
            }
            break;
        case CmdCommit:
        case CmdReset:
            emit owner->committed(status == 0);
            break;
        case CmdGetPotCfg:
            if (status == 0) {
                emit owner->potConfigLoaded(frame[3], frame[4], frame[5], frame[6]);
            }
            break;
        case CmdGetPotValue:
            if (status == 0) {
                const int raw = frame[3] | (frame[4] << 8);
                const int semantic = (int16_t)(frame[6] | (frame[7] << 8));
                emit owner->potValue(raw, frame[5], semantic);
            }
            break;
        default:
            break;
        }
    }

    void send(uint8_t cmd, const uint8_t *payload, size_t payloadLen) {
        if (!device) {
            return;
        }
        uint8_t frame[kFrameSize] = {};
        frame[0] = cmd;
        frame[1] = ++seq;
        if (payload && payloadLen > 0) {
            memcpy(&frame[2], payload, MIN(payloadLen, kFrameSize - 2));
        }
        IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, frame, sizeof(frame));
    }
};

KnobHidChannel::KnobHidChannel(QObject *parent) : QObject(parent), impl_(new Impl) {
    impl_->owner = this;

    impl_->manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
    NSDictionary *match = @{
        @(kIOHIDVendorIDKey): @(kVendorId),
        @(kIOHIDProductIDKey): @(kProductId),
        @(kIOHIDDeviceUsagePageKey): @(kUsagePage),
        @(kIOHIDDeviceUsageKey): @(kUsage),
    };
    IOHIDManagerSetDeviceMatching(impl_->manager, (__bridge CFDictionaryRef)match);
    IOHIDManagerRegisterDeviceMatchingCallback(
        impl_->manager,
        [](void *ctx, IOReturn, void *, IOHIDDeviceRef dev) {
            static_cast<Impl *>(ctx)->onDeviceMatched(dev);
        },
        impl_);
    IOHIDManagerRegisterDeviceRemovalCallback(
        impl_->manager,
        [](void *ctx, IOReturn, void *, IOHIDDeviceRef dev) {
            static_cast<Impl *>(ctx)->onDeviceRemoved(dev);
        },
        impl_);
    IOHIDManagerScheduleWithRunLoop(impl_->manager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    IOHIDManagerOpen(impl_->manager, kIOHIDOptionsTypeNone);
}

KnobHidChannel::~KnobHidChannel() {
    if (impl_->manager) {
        IOHIDManagerUnscheduleFromRunLoop(impl_->manager, CFRunLoopGetMain(),
                                          kCFRunLoopDefaultMode);
        IOHIDManagerClose(impl_->manager, kIOHIDOptionsTypeNone);
        CFRelease(impl_->manager);
    }
    delete impl_;
}

bool KnobHidChannel::channelPresent() const {
    return impl_->device != nullptr;
}

void KnobHidChannel::deviceDropped() {
    // macOS: IOHIDManager's removal callback handles disconnects.
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
