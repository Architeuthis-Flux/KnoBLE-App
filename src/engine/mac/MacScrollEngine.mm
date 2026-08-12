// macOS scroll engine. See MacScrollEngine.h for the architecture summary.
//
// Ground truth for the synthesis recipe is LinearMouse (MIT), which this
// project's firmware docs already treat as the reference for macOS scroll
// behavior: continuous (pixel) scroll events carrying gesture phase fields,
// integerized with a carried remainder, marked via the event-source user
// data field so our own tap passes them through.

#include "engine/mac/MacScrollEngine.h"
#include "engine/KnobScrollModel.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <future>

// Private CoreGraphics/IOKit SPI, stable for years and relied on by
// LinearMouse and Mos: recover which HID service sent a CGEvent. This is the
// only way to tell knob wheel events from mouse wheel events at the tap.
extern "C" {
typedef struct __IOHIDEvent *IOHIDEventRef;
IOHIDEventRef CGEventCopyIOHIDEvent(CGEventRef);
uint64_t IOHIDEventGetSenderID(IOHIDEventRef);
}

namespace {

// Marks events we posted so the tap never re-processes them. ('KNOB')
constexpr int64_t kSyntheticMarker = 0x4B4E4F42;

constexpr double kTickHz = 120.0;
// Legacy line-delta approximation for synthetic continuous events: apps that
// only read the line field see ~12 px per line (LinearMouse's output step).
constexpr double kPointsPerLine = 12.0;

double nowSeconds() {
    return [NSProcessInfo processInfo].systemUptime;
}

} // namespace

struct MacScrollEngine::Impl {
    std::vector<DeviceFilter> filters;

    // --- settings, written by the GUI thread, read by the engine thread ---
    std::atomic<double> pxPerCount{18.0};
    std::atomic<double> tauMs{60.0};
    std::atomic<bool> invert{false};
    std::atomic<bool> reportPhases{true};
    std::atomic<bool> enabled{true};

    // --- engine-thread state ---
    std::thread thread;
    std::atomic<bool> running{false}; // tap created and runloop spinning
    CFRunLoopRef runLoop = nullptr;
    CFMachPortRef tap = nullptr;
    CFRunLoopSourceRef tapSource = nullptr;
    CFRunLoopTimerRef timer = nullptr;
    IOHIDManagerRef hidManager = nullptr;

    KnobScrollModel model;
    PixelDeltaAccumulator accumulator;
    CGEventFlags lastFlags = 0;
    double lastTickSec = 0.0;
    bool gestureOpen = false;

    // senderID -> is-the-knob verdict, cleared on device add/remove.
    std::unordered_map<uint64_t, bool> senderVerdicts;
    std::mutex senderMutex;

    std::function<void(bool)> *deviceConnectedCb = nullptr;
    std::function<void()> *permissionMissingCb = nullptr;
    std::function<void(bool)> *rawCountsActiveCb = nullptr;

    // True when IOHIDManagerOpen succeeded (Input Monitoring granted): wheel
    // counts come from the device's own HID values, pre-acceleration. False:
    // fall back to tapped-event values, which carry the OS curve.
    std::atomic<bool> rawCounts{false};

    // ---------------- device identification ----------------

    bool matchesFilters(int64_t vendor, int64_t product) const {
        for (const auto &f : filters) {
            if (vendor == f.vendorId && product == f.productId) {
                return true;
            }
        }
        return false;
    }

    // Resolve a HID sender ID to a registry entry and search it and its
    // ancestors for the USB/BLE vendor and product IDs. Public IOKit only.
    bool senderIsKnob(uint64_t senderID) {
        {
            std::lock_guard<std::mutex> lock(senderMutex);
            auto it = senderVerdicts.find(senderID);
            if (it != senderVerdicts.end()) {
                return it->second;
            }
        }

        bool verdict = false;
        io_service_t service =
            IOServiceGetMatchingService(MACH_PORT_NULL, IORegistryEntryIDMatching(senderID));
        if (service != IO_OBJECT_NULL) {
            const uint32_t searchOptions = kIORegistryIterateRecursively | kIORegistryIterateParents;
            CFTypeRef vendorRef = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, CFSTR("VendorID"), kCFAllocatorDefault, searchOptions);
            CFTypeRef productRef = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, CFSTR("ProductID"), kCFAllocatorDefault, searchOptions);

            int64_t vendor = -1;
            int64_t product = -1;
            if (vendorRef && CFGetTypeID(vendorRef) == CFNumberGetTypeID()) {
                CFNumberGetValue((CFNumberRef)vendorRef, kCFNumberSInt64Type, &vendor);
            }
            if (productRef && CFGetTypeID(productRef) == CFNumberGetTypeID()) {
                CFNumberGetValue((CFNumberRef)productRef, kCFNumberSInt64Type, &product);
            }
            if (vendorRef) {
                CFRelease(vendorRef);
            }
            if (productRef) {
                CFRelease(productRef);
            }
            IOObjectRelease(service);

            verdict = matchesFilters(vendor, product);
        }

        std::lock_guard<std::mutex> lock(senderMutex);
        senderVerdicts[senderID] = verdict;
        return verdict;
    }

    void clearSenderCache() {
        std::lock_guard<std::mutex> lock(senderMutex);
        senderVerdicts.clear();
    }

    // ---------------- synthesis ----------------

    void postScroll(double deltaPx, KnobScrollModel::Phase phase) {
        const double delivered = accumulator.take(deltaPx);
        const bool phases = reportPhases.load(std::memory_order_relaxed);
        const bool isEnd = phase == KnobScrollModel::Phase::Ended;

        if (delivered == 0.0 && !isEnd) {
            return; // nothing visible to say yet; keep accumulating
        }
        if (isEnd && !gestureOpen && delivered == 0.0) {
            return;
        }

        CGEventRef event =
            CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 2, 0, 0);
        if (!event) {
            return;
        }

        CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);
        CGEventSetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1,
                                    (int64_t)(delivered / kPointsPerLine));
        CGEventSetDoubleValueField(event, kCGScrollWheelEventPointDeltaAxis1, delivered);
        CGEventSetDoubleValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1, delivered);

        if (phases) {
            if (isEnd && !gestureOpen) {
                // Whole session fit in one event: a Began with no Ended would
                // leave apps holding a dangling gesture. Post it phase-less.
            } else {
                CGScrollPhase scrollPhase;
                if (!gestureOpen) {
                    scrollPhase = kCGScrollPhaseBegan;
                    gestureOpen = true;
                } else if (isEnd) {
                    scrollPhase = kCGScrollPhaseEnded;
                } else {
                    scrollPhase = kCGScrollPhaseChanged;
                }
                CGEventSetIntegerValueField(event, kCGScrollWheelEventScrollPhase, scrollPhase);
                CGEventSetIntegerValueField(event, kCGScrollWheelEventMomentumPhase, 0);
            }
        }
        if (isEnd) {
            gestureOpen = false;
        }

        CGEventSetIntegerValueField(event, kCGEventSourceUserData, kSyntheticMarker);
        CGEventSetFlags(event, lastFlags);

        CGEventPost(kCGSessionEventTap, event);
        CFRelease(event);
    }

    // Common input path for both sources; engine-thread only.
    void feedCounts(int counts) {
        if (counts == 0 || !enabled.load(std::memory_order_relaxed)) {
            return;
        }
        if (invert.load(std::memory_order_relaxed)) {
            counts = -counts;
        }
        const double now = nowSeconds();
        if (!model.active()) {
            lastTickSec = now;
        }
        model.feed(counts, now);
        armTimer();
    }

    void tick() {
        if (!model.active()) {
            idleTimer();
            return;
        }

        const double now = nowSeconds();
        double dt = now - lastTickSec;
        if (dt < 1.0 / 240.0) {
            dt = 1.0 / 240.0;
        } else if (dt > 1.0 / 24.0) {
            dt = 1.0 / 24.0;
        }
        lastTickSec = now;

        model.setParams({pxPerCount.load(std::memory_order_relaxed),
                         tauMs.load(std::memory_order_relaxed)});

        const auto emission = model.tick(now, dt);
        if (emission.phase == KnobScrollModel::Phase::None && emission.deltaPx == 0.0) {
            return;
        }
        postScroll(emission.deltaPx, emission.phase);

        if (emission.phase == KnobScrollModel::Phase::Ended) {
            accumulator.reset();
            idleTimer();
        }
    }

    void armTimer() {
        if (timer) {
            CFRunLoopTimerSetNextFireDate(timer, CFAbsoluteTimeGetCurrent());
        }
    }

    void idleTimer() {
        if (timer) {
            CFRunLoopTimerSetNextFireDate(timer, CFAbsoluteTimeGetCurrent() + 3600.0 * 24 * 365);
        }
    }

    // ---------------- event tap ----------------

    CGEventRef handleTap(CGEventTapProxy, CGEventType type, CGEventRef event) {
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            if (tap) {
                CGEventTapEnable(tap, true);
            }
            return event;
        }
        if (type != kCGEventScrollWheel || !enabled.load(std::memory_order_relaxed)) {
            return event;
        }
        if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == kSyntheticMarker) {
            return event; // one of ours
        }

        uint64_t senderID = 0;
        if (IOHIDEventRef hidEvent = CGEventCopyIOHIDEvent(event)) {
            senderID = IOHIDEventGetSenderID(hidEvent);
            CFRelease(hidEvent);
        }
        if (senderID == 0 || !senderIsKnob(senderID)) {
            return event; // not the knob: never touch other devices
        }

        lastFlags = CGEventGetFlags(event);

        if (rawCounts.load(std::memory_order_relaxed)) {
            // Counts arrive via the HID value callback (pre-acceleration);
            // here we only suppress the OS-cooked duplicate.
            return nullptr;
        }

        // Fallback (no Input Monitoring): use the tapped value. macOS has
        // already applied its acceleration curve to this field — smoothing
        // still works but pacing follows the OS curve.
        int64_t counts = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
        if (counts == 0) {
            // No vertical action (e.g. a future horizontal-wheel profile):
            // pass through untouched, we only own the vertical axis.
            return event;
        }
        feedCounts((int)counts);

        return nullptr; // swallow the discrete event; the timer re-emits it
    }

    // ---------------- device presence (public IOHIDManager) ----------------

    void setupHidWatch() {
        hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
        if (!hidManager) {
            return;
        }

        CFMutableArrayRef matches =
            CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        for (const auto &f : filters) {
            CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
                kCFAllocatorDefault, 2, &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);
            int vendor = f.vendorId;
            int product = f.productId;
            CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
            CFNumberRef p = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
            CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), v);
            CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), p);
            CFRelease(v);
            CFRelease(p);
            CFArrayAppendValue(matches, dict);
            CFRelease(dict);
        }
        IOHIDManagerSetDeviceMatchingMultiple(hidManager, matches);
        CFRelease(matches);

        IOHIDManagerRegisterDeviceMatchingCallback(
            hidManager,
            [](void *ctx, IOReturn, void *, IOHIDDeviceRef) {
                static_cast<Impl *>(ctx)->onDevicesChanged();
            },
            this);
        IOHIDManagerRegisterDeviceRemovalCallback(
            hidManager,
            [](void *ctx, IOReturn, void *, IOHIDDeviceRef) {
                static_cast<Impl *>(ctx)->onDevicesChanged();
            },
            this);
        IOHIDManagerScheduleWithRunLoop(hidManager, runLoop, kCFRunLoopDefaultMode);

        // Raw wheel values, straight from the device: immune to the OS
        // acceleration curve, which is baked into tapped CGEvent deltas
        // before we ever see them (LinearMouse's only magnitude-preserving
        // linear path does the same via raw input reports). Only the wheel
        // element is matched; parsed values, no report-layout guessing.
        NSDictionary *wheelMatch = @{
            @(kIOHIDElementUsagePageKey): @(kHIDPage_GenericDesktop),
            @(kIOHIDElementUsageKey): @(kHIDUsage_GD_Wheel),
        };
        IOHIDManagerSetInputValueMatching(hidManager, (__bridge CFDictionaryRef)wheelMatch);
        IOHIDManagerRegisterInputValueCallback(
            hidManager,
            [](void *ctx, IOReturn, void *, IOHIDValueRef value) {
                auto *self = static_cast<Impl *>(ctx);
                if (!self->rawCounts.load(std::memory_order_relaxed)) {
                    return; // fallback mode: the tap feeds instead
                }
                self->feedCounts((int)IOHIDValueGetIntegerValue(value));
            },
            this);

        tryOpenForRawCounts();
    }

    // Opening the manager (Input Monitoring TCC) upgrades us from tapped
    // values to raw device counts. Retryable after the user grants.
    void tryOpenForRawCounts() {
        if (!hidManager || rawCounts.load(std::memory_order_relaxed)) {
            return;
        }
        const bool ok = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone) == kIOReturnSuccess;
        rawCounts.store(ok, std::memory_order_relaxed);
        if (rawCountsActiveCb && *rawCountsActiveCb) {
            (*rawCountsActiveCb)(ok);
        }
    }

    void onDevicesChanged() {
        clearSenderCache();
        notifyPresence();
    }

    void notifyPresence() {
        bool present = false;
        if (hidManager) {
            CFSetRef set = IOHIDManagerCopyDevices(hidManager);
            if (set) {
                present = CFSetGetCount(set) > 0;
                CFRelease(set);
            }
        }
        if (deviceConnectedCb && *deviceConnectedCb) {
            (*deviceConnectedCb)(present);
        }
    }

    // ---------------- lifecycle ----------------

    bool startThread() {
        std::promise<bool> ready;
        auto readyFuture = ready.get_future();

        thread = std::thread([this, &ready] {
            runLoop = CFRunLoopGetCurrent();

            tap = CGEventTapCreate(
                kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                CGEventMaskBit(kCGEventScrollWheel),
                [](CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *ctx)
                    -> CGEventRef {
                    return static_cast<Impl *>(ctx)->handleTap(proxy, type, event);
                },
                this);
            if (!tap) {
                // A thread's CFRunLoop is freed at thread exit — leaving the
                // pointer set would hand stop() a dangling runloop.
                runLoop = nullptr;
                ready.set_value(false);
                return;
            }
            tapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
            CFRunLoopAddSource(runLoop, tapSource, kCFRunLoopCommonModes);

            CFRunLoopTimerContext timerCtx = {0, this, nullptr, nullptr, nullptr};
            timer = CFRunLoopTimerCreate(
                kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 3600.0 * 24 * 365,
                1.0 / kTickHz, 0, 0,
                [](CFRunLoopTimerRef, void *ctx) { static_cast<Impl *>(ctx)->tick(); },
                &timerCtx);
            CFRunLoopAddTimer(runLoop, timer, kCFRunLoopCommonModes);

            setupHidWatch();
            notifyPresence();

            running.store(true);
            ready.set_value(true);
            CFRunLoopRun();

            // teardown on this thread after CFRunLoopStop()
            if (hidManager) {
                if (rawCounts.load(std::memory_order_relaxed)) {
                    IOHIDManagerClose(hidManager, kIOHIDOptionsTypeNone);
                }
                IOHIDManagerUnscheduleFromRunLoop(hidManager, runLoop, kCFRunLoopDefaultMode);
                CFRelease(hidManager);
                hidManager = nullptr;
            }
            if (timer) {
                CFRunLoopTimerInvalidate(timer);
                CFRelease(timer);
                timer = nullptr;
            }
            if (tapSource) {
                CFRunLoopRemoveSource(runLoop, tapSource, kCFRunLoopCommonModes);
                CFRelease(tapSource);
                tapSource = nullptr;
            }
            if (tap) {
                CGEventTapEnable(tap, false);
                CFMachPortInvalidate(tap);
                CFRelease(tap);
                tap = nullptr;
            }
            runLoop = nullptr;
            running.store(false);
        });

        return readyFuture.get();
    }

    void stopThread() {
        if (runLoop) {
            CFRunLoopStop(runLoop);
        }
        if (thread.joinable()) {
            thread.join();
        }
    }
};

MacScrollEngine::MacScrollEngine(std::vector<DeviceFilter> devices)
    : impl_(std::make_unique<Impl>()) {
    impl_->filters = std::move(devices);
    impl_->deviceConnectedCb = &deviceConnected;
    impl_->permissionMissingCb = &permissionMissing;
    impl_->rawCountsActiveCb = &rawCountsActive;
}

MacScrollEngine::~MacScrollEngine() {
    stop();
}

bool MacScrollEngine::start() {
    // Event taps need the Accessibility permission. Prompt on first ask; the
    // user grants it in System Settings and hits "retry" in our UI.
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
    if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options)) {
        if (permissionMissing) {
            permissionMissing();
        }
        return false;
    }

    if (impl_->running.load()) {
        // Already running; the user may have just granted Input Monitoring —
        // retry the raw-counts upgrade on the engine thread.
        if (impl_->runLoop) {
            CFRunLoopRef rl = impl_->runLoop;
            Impl *impl = impl_.get();
            CFRunLoopPerformBlock(rl, kCFRunLoopDefaultMode, ^{
                impl->tryOpenForRawCounts();
            });
            CFRunLoopWakeUp(rl);
        }
        return true;
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join(); // reap a thread left over from a failed start
    }
    if (!impl_->startThread()) {
        if (permissionMissing) {
            permissionMissing();
        }
        return false;
    }
    return true;
}

void MacScrollEngine::stop() {
    impl_->stopThread();
}

void MacScrollEngine::applySettings(const ScrollEngineSettings &settings) {
    impl_->pxPerCount.store(settings.pxPerCount, std::memory_order_relaxed);
    impl_->tauMs.store(settings.responseMs, std::memory_order_relaxed);
    impl_->invert.store(settings.invert, std::memory_order_relaxed);
    impl_->reportPhases.store(settings.reportPhases, std::memory_order_relaxed);
    impl_->enabled.store(settings.enabled, std::memory_order_relaxed);
}
