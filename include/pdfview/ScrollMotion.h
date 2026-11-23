#pragma once

// Smooth scrolling and middle-button autoscroll, without input handling or
// drawing: the caller feeds in input samples, elapsed time and bounds, and
// owns everything else (ImGui input ownership, the marker, page turns).
// Free of ImGui, so it can move to Hd once a second project needs it.

#include <algorithm>
#include <cmath>

namespace pdfview {

struct ScrollVec {
    double x = 0;
    double y = 0;
};

// Smooth scrolling as a critically damped spring that pulls the position
// towards a target.
// - Velocity never jumps except by an explicit impulse, so new input taken
//   mid-glide bends the motion instead of restarting a fixed curve.
// - An input with impulse 1 (wheel notches, arrow keys) starts at full speed
//   and decays exponentially, like a flick that friction stops. An input with
//   impulse 0 (Page Down) speeds up gently from rest and settles the same way.
// - The spring's pull saturates, so long moves cruise at maxSpeed and then
//   brake without overshooting.
// - It integrates in small fixed substeps, so the path doesn't depend on the
//   frame rate. The first step counts as one 60 Hz frame at most, since it
//   can follow a long idle wait for input.
class ScrollMotion {
public:
    using Vec = ScrollVec;

    // Suggested glides, in screen pixels at UI scale 1. Steps (wheel, lines)
    // start at full speed and decay: 90 % of the way in 130 ms, at rest in
    // about 300 ms. Screenfuls speed up from rest, cruise at the cap and
    // brake: a 900-pixel one is 90 % done in 280 ms and at rest in about
    // 500 ms. A held page key stops about 200 pixels after release.
    static constexpr double kStepStiffness = 18; // per second
    static constexpr double kPageStiffness = 32;
    static constexpr double kMaxSpeed = 3200; // pixels per second

    struct Params {
        double stiffness = kStepStiffness; // 1/s: the decay rate; higher settles sooner
        double impulse = 1; // 0..1: how much of a new step becomes speed at once
        double maxSpeed = kMaxSpeed; // units per second
    };

    // How far ahead of the position the pull saturates. A target kept this
    // far ahead cruises at maxSpeed, and when it stops moving the motion
    // brakes from full speed to rest on it.
    static double brakingDistance(const Params& p) { return 2 * p.maxSpeed / p.stiffness; }

    bool active() const { return mActive; }
    Vec target() const { return mTarget; }
    Vec velocity() const { return mVel; }

    // Sets a new target. `pos` is where the content is now; it starts the
    // motion when it is at rest.
    void moveTo(Vec pos, Vec target, Params p)
    {
        Vec from = mActive ? mTarget : pos;
        if (!mActive) {
            mVel = {};
            mFresh = true;
        }
        mParams = p;
        mTarget = target;
        double k = std::clamp(p.impulse, 0.0, 1.0) * p.stiffness;
        mVel.x += k * (target.x - from.x);
        mVel.y += k * (target.y - from.y);
        limitSpeed();
        mActive = true;
    }

    // Drops the motion where it is.
    void stop()
    {
        mActive = false;
        mVel = {};
    }

    // Advances `pos` by `dt` seconds. It lands exactly on the target once it
    // is within `epsilon` units and slower than `epsilon` * 20 units/s.
    // Returns whether the motion goes on.
    bool step(Vec& pos, double dt, double epsilon)
    {
        if (!mActive) return false;
        if (mFresh) dt = std::min(dt, 1.0 / 60);
        mFresh = false;
        constexpr double substep = 1.0 / 1000;
        const double w = mParams.stiffness;
        // the pull is capped so its terminal speed is maxSpeed
        const double reach = brakingDistance(mParams);
        while (dt > 0) {
            double h = std::min(dt, substep);
            dt -= h;
            Vec d{mTarget.x - pos.x, mTarget.y - pos.y};
            double len = std::hypot(d.x, d.y);
            double scale = len > reach ? reach / len : 1;
            // semi-implicit Euler: stable for w * h well below 1
            mVel.x += (w * w * d.x * scale - 2 * w * mVel.x) * h;
            mVel.y += (w * w * d.y * scale - 2 * w * mVel.y) * h;
            limitSpeed();
            pos.x += mVel.x * h;
            pos.y += mVel.y * h;
        }
        double left = std::hypot(mTarget.x - pos.x, mTarget.y - pos.y);
        if (left <= epsilon && std::hypot(mVel.x, mVel.y) <= epsilon * 20) {
            pos = mTarget;
            stop();
        }
        return mActive;
    }

private:
    void limitSpeed()
    {
        double s = std::hypot(mVel.x, mVel.y);
        if (s > mParams.maxSpeed && s > 0) {
            mVel.x *= mParams.maxSpeed / s;
            mVel.y *= mParams.maxSpeed / s;
        }
    }

    Params mParams;
    Vec mTarget;
    Vec mVel;
    bool mActive = false;
    bool mFresh = false; // no step taken yet
};

// Middle-button autoscroll speed, in pixels per second, for a pointer
// `offset` pixels from where autoscroll started: a dead zone, then an
// exponential rise to a limit. `scale` is the UI scale.
inline double autoscrollSpeed(double offset, double scale)
{
    constexpr double initialSensitivity = 5;
    constexpr double growthDistance = 200;
    constexpr double speedLimit = 5000;
    double beyondDeadZone = std::clamp(std::abs(offset) / scale - 10, 0.0, speedLimit / initialSensitivity);
    double speed = initialSensitivity * growthDistance * std::expm1(beyondDeadZone / growthDistance);
    return std::copysign(std::min(speed, speedLimit) * scale, offset);
}

// The middle-button autoscroll gesture. A press that is released without
// moving latches it until the caller stops it (a click, the wheel, a key);
// releasing after a drag stops it.
class Autoscroll {
public:
    bool on() const { return mOn; }
    ScrollVec origin() const { return mOrigin; }

    // the button went down with the pointer at `at`
    void start(ScrollVec at)
    {
        mOn = mHeld = true;
        mDragged = false;
        mOrigin = at;
    }
    void stop() { mOn = mHeld = false; }

    // Each frame while on: the pointer, and whether the button that started
    // it is still down. `scale` is the UI scale, for the drag threshold.
    void track(ScrollVec pointer, bool buttonDown, double scale = 1)
    {
        if (!mOn || !mHeld) return;
        double dx = pointer.x - mOrigin.x, dy = pointer.y - mOrigin.y;
        double slop = 3 * scale;
        if (dx * dx + dy * dy > slop * slop) mDragged = true;
        if (!buttonDown) {
            mHeld = false;
            if (mDragged) mOn = false;
        }
    }

    // the scroll velocity, in pixels per second, for the pointer at `pointer`
    ScrollVec velocity(ScrollVec pointer, double scale) const
    {
        if (!mOn) return {};
        return {autoscrollSpeed(pointer.x - mOrigin.x, scale), autoscrollSpeed(pointer.y - mOrigin.y, scale)};
    }

private:
    ScrollVec mOrigin;
    bool mOn = false;
    bool mHeld = false; // the button that started it is still down
    bool mDragged = false;
};

} // namespace pdfview
