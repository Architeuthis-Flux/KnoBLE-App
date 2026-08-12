// Knob scroll model — portable core shared by every OS backend.
//
// The knob reports exact rotation quanta (wheel counts). Unlike a generic
// scroll smoother, we never need to *guess* intent from event rates: distance
// scrolled must equal rotation turned, always. So the model is a pure
// position-target follower:
//
//   target += counts * pxPerCount          (input, exact)
//   pos    -> target with a first-order lag (animation, tau = "response")
//
// Strictly linear: no acceleration curve, no velocity estimation, no synthetic
// momentum. The knob's own flywheel is the only physics. This is what makes
// slow turns at high speed settings glide instead of hopping N lines per
// count: each count becomes an animated pixel distance, not a discrete jump.

#pragma once

#include <cmath>

class KnobScrollModel {
public:
    enum class Phase { None, Began, Changed, Ended };

    struct Emission {
        double deltaPx = 0.0; // pixels to post this tick (fractional)
        Phase phase = Phase::None;
    };

    struct Params {
        double pxPerCount = 18.0; // scroll distance per wheel count
        double tauMs = 60.0;      // catch-up time constant (smaller = snappier)
    };

    void setParams(const Params &p) { params_ = p; }
    const Params &params() const { return params_; }

    // Wheel counts from the device (signed). Call from the input thread.
    void feed(int counts, double nowSec) {
        target_ += static_cast<double>(counts) * params_.pxPerCount;
        lastInputSec_ = nowSec;
        active_ = true;
    }

    bool active() const { return active_; }

    // Advance the animation. Call at the emission rate (e.g. 120 Hz) while
    // active(); returns the pixels to post and the gesture phase to tag.
    Emission tick(double nowSec, double dtSec) {
        Emission out;
        if (!active_) {
            return out;
        }

        const double remaining = target_ - pos_;
        const double tau = params_.tauMs / 1000.0;
        // First-order lag: exponential approach, unconditionally stable for
        // any dt. pos never overshoots target — the page is glued to the knob.
        const double step = remaining * (1.0 - std::exp(-dtSec / (tau > 1e-4 ? tau : 1e-4)));
        pos_ += step;
        out.deltaPx = step;

        const bool settled = std::fabs(target_ - pos_) < kSettledPx;
        const bool inputFresh = (nowSec - lastInputSec_) <= kInputGraceSec;

        if (settled && !inputFresh) {
            // Deliver the sub-pixel residual in the final event so distance
            // stays exact across a session, then close the gesture.
            out.deltaPx = target_ - (pos_ - step);
            pos_ = target_;
            out.phase = began_ ? Phase::Ended : Phase::None;
            reset();
            return out;
        }

        if (std::fabs(out.deltaPx) < kEmitThresholdPx && began_) {
            out.deltaPx = 0.0;
            out.phase = Phase::Changed; // keep the gesture alive while idle-ish
            return out;
        }

        out.phase = began_ ? Phase::Changed : Phase::Began;
        began_ = true;
        return out;
    }

private:
    static constexpr double kSettledPx = 0.25;
    static constexpr double kEmitThresholdPx = 0.01;
    static constexpr double kInputGraceSec = 0.080; // knob poll gaps < 80 ms

    void reset() {
        target_ = 0.0;
        pos_ = 0.0;
        began_ = false;
        active_ = false;
    }

    Params params_;
    double target_ = 0.0;
    double pos_ = 0.0;
    double lastInputSec_ = 0.0;
    bool began_ = false;
    bool active_ = false;
};

// Integerizes fractional pixel deltas with a carried remainder so no distance
// is lost between events (matches how LinearMouse delivers synthetic scroll).
class PixelDeltaAccumulator {
public:
    double take(double value) {
        const double combined = value + remainder_;
        const double delivered = std::trunc(combined);
        remainder_ = combined - delivered;
        return delivered;
    }

    void reset() { remainder_ = 0.0; }

private:
    double remainder_ = 0.0;
};
