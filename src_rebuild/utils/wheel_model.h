#pragma once

// Host-independent drivetrain and calibration math. Clutch: 1 = pedal down.
#include <cmath>
#include <algorithm>

namespace racing {
inline float clamp(float x, float lo, float hi) { return std::max(lo, std::min(x, hi)); }
struct Axis {
    int index, minimum, center, maximum;
    float deadzone, gamma;
    bool inverted;
    Axis(int i = -1, bool inv = false) : index(i), minimum(-32768), center(0),
        maximum(32767), deadzone(0.015f), gamma(1), inverted(inv) {}
    float read(int raw, bool steering) const {
        float x;
        if (steering) {
            const int span = raw >= center ? maximum - center : center - minimum;
            x = span > 0 ? float(raw - center) / span : 0;
        } else x = maximum > minimum ? float(raw - minimum) / (maximum - minimum) : 0;
        if (inverted) x = steering ? -x : 1 - x;
        float sign = x < 0 ? -1.f : 1.f;
        x = clamp((std::fabs(x) - clamp(deadzone, 0, .4f)) / (1 - clamp(deadzone, 0, .4f)), 0, 1);
        return sign * std::pow(x, clamp(gamma, .2f, 5));
    }
};
struct Input {
    float steering = 0, throttle = 0, brake = 0, clutch = 1, handbrake = 0;
    int gear = 0; // -1 reverse, 0 neutral, 1..5 forward
};
struct Setup {
    float ratios[6] = {3.3f, 3.3f, 2.1f, 1.5f, 1.15f, .9f};
    float finalDrive = 3.7f, tyreRadius = .34f;
    float idle = 850, redline = 6500, bite = .65f;
    bool assisted = true, burnout = true;
};
struct Output {
    float drive = 0, engineBrake = 0, rpm = 850;
    int gear = 0;
    bool burnout = false, stalled = false, rejectedShift = false;
};
struct Transmission {
    Output out;
    float stallTime = 0, shiftTime = 0;
    int requested = 0;
    bool burnoutFinished = false;
    void reset() { *this = Transmission(); }
    Output step(const Input& in, float speedMS, float dt, const Setup& cfg) {
        dt = clamp(dt, 0, .1f);
        out.rejectedShift = false;
        if (in.gear != requested) { requested = in.gear; shiftTime = .12f; }
        if (in.gear == 0) out.gear = 0;
        else if (in.gear != out.gear) {
            const bool directionOK = (in.gear > 0 || speedMS < 1.5f) && (in.gear < 0 || speedMS > -1.5f);
            if (directionOK && (cfg.assisted || in.clutch > cfg.bite)) out.gear = in.gear;
            else out.rejectedShift = true;
        }
        shiftTime = std::max(0.f, shiftTime - dt);
        float engagement = clamp(1 - in.clutch / std::max(.1f, cfg.bite), 0, 1);
        if (cfg.assisted) engagement = shiftTime > 0 ? 0.f : std::min(engagement, clamp(.35f + std::fabs(speedMS) * .15f + in.throttle * .4f, 0, 1));
        const int idx = out.gear < 0 ? 0 : std::max(1, std::min(5, out.gear));
        const float ratio = cfg.ratios[idx] * cfg.finalDrive;
        const float wheelRPM = std::fabs(speedMS) * 60 / (6.2831853f * std::max(.1f, cfg.tyreRadius));
        const float coupledRPM = wheelRPM * ratio;
        const float freeRPM = cfg.idle + in.throttle * (cfg.redline + 300 - cfg.idle);
        float target = out.gear ? freeRPM * (1-engagement) + coupledRPM * engagement : freeRPM;
        if (cfg.assisted) target = std::max(cfg.idle, target);
        out.rpm += (target - out.rpm) * clamp(dt * 10, 0, 1);
        if (!cfg.assisted && out.gear && engagement > .8f && out.rpm < 550) stallTime += dt;
        else stallTime = 0;
        if (stallTime > .3f) out.stalled = true;
        if (out.stalled && (in.clutch > .9f || out.gear == 0) && in.throttle > .1f) {
            out.stalled = false; out.rpm = cfg.idle; stallTime = 0;
        }
        if (out.stalled) out.rpm = 0;
        const float torqueCurve = clamp(1 - .45f * std::pow((out.rpm - 3500) / 3500, 2.f), .2f, 1);
        const float revLimit = out.rpm > cfg.redline ? 0.f : 1.f;
        // Driver's stock thrust already includes its acceleration envelope. Applying
        // the entire gear reduction again removed 73% of fifth-gear propulsion.
        const float gearing = clamp(cfg.ratios[idx] / cfg.ratios[1], 1.f, 1.3f);
        const float lugging = idx == 1 ? 1.f : clamp((out.rpm-500)/1500, .2f, 1);
        const float delivery = idx >= 3 ? 1.f : torqueCurve;
        out.drive = out.gear && !out.stalled ? in.throttle * engagement * delivery * gearing * lugging * revLimit : 0;
        if (out.gear < 0) out.drive = -out.drive;
        out.engineBrake = out.gear ? engagement * (1-in.throttle) * .12f * cfg.ratios[idx] / cfg.ratios[1] : 0;
        if(in.throttle < .9f || out.gear != 1) burnoutFinished=false;
        // Original CAR_PAD_WHEELSPIN cutoff: 452952 fixed-point velocity units.
        const float burnoutCutoff = 452952.f/4096*.3f;
        if(std::fabs(speedMS) >= burnoutCutoff) burnoutFinished=true;
        bool launch = cfg.burnout && out.gear == 1 && in.throttle > .97f && engagement > .35f && !out.stalled;
        out.burnout = launch && !burnoutFinished;
        if(launch) {
            // Circle becomes X above the cutoff, even if first remains selected.
            // Do not let the synthetic first-gear RPM limiter interrupt that mode.
            out.drive=in.throttle*engagement*(out.burnout?5000.f/4915:1.f);
            out.rpm=clamp(out.rpm,3500,cfg.redline*.95f);
        }
        return out;
    }
};
// ---------------------------------------------------------------------------
// Force feedback. Normalized torque: +1 is the full device force towards
// positive steering. Driver 2 runs at 30 Hz, so every value is per game frame.
struct ForceSetup {
    float gain = .80f;       // master strength, applied before soft clipping
    float filter = .70f;     // smoothing of the tyre torque per frame (1 = none)
    float road = .25f;       // suspension and surface detail
    float minimum = .08f;    // low-force dead band compensation (gear-driven wheels)
    float damping = .12f;    // device damper (software fallback if unsupported)
    float friction = .15f;   // device friction at standstill (tyre scrub)
    float caster = .25f;     // mechanical trail, relative to pneumatic trail at zero slip
    float selfCenter = .08f; // caster jacking spring, independent of speed
    float load = .50f;       // sensitivity to the game's front grip transfer
    float knee = .75f;       // soft clipping starts above this level
};
struct ForceFrame {
    float torque = 0;        // tyre and steering geometry torque
    float road = 0;          // unfiltered detail
    float speed = 0;         // m/s
};
struct ForceReport { float output = 0; bool clipping = false; };

// Self-aligning torque of one front tyre. slip is Driver 2's lateral slip
// normalised to its friction limit: the lateral force rises linearly with it
// and saturates at |1|. The pneumatic trail shrinks as the contact patch starts
// sliding from the rear while the mechanical (caster) trail remains, so the
// torque peaks before the grip limit and goes light beyond it, like a real tyre.
// The result is normalised so that 1 is the peak.
inline float alignTorque(float slip, float caster) {
    const float s = clamp(std::fabs(slip), 0, 1);
    const float m = clamp(caster, 0, 2);
    const float peakSlip = std::min(1.f, std::sqrt((1 + m) / 3));
    const float peak = peakSlip * (1 - peakSlip * peakSlip + m);
    const float t = s * (1 - s * s + m) / peak;
    return slip < 0 ? -t : t;
}
// Linear up to the knee, then compressed smoothly towards 1 instead of cutting
// every peak to the same flat value (hard clipping erases information).
inline float softClip(float x, float knee) {
    knee = clamp(knee, .3f, 1);
    const float a = std::fabs(x);
    if (a <= knee || knee >= 1) return clamp(x, -1, 1);
    const float y = knee + (1 - knee) * std::tanh((a - knee) / (1 - knee));
    return x < 0 ? -y : y;
}
// Lift small forces above the motor's dead band. The boost fades in over the
// first 3 % so that there is no force step at zero, which makes the centre chatter.
inline float deadbandCompensate(float x, float minimum) {
    minimum = clamp(minimum, 0, .25f);
    const float a = clamp(std::fabs(x), 0, 1);
    const float y = (minimum + (1 - minimum) * a) * std::min(1.f, a / .03f);
    return x < 0 ? -y : y;
}
struct ConditionLevels { float damper = 0, friction = 0; };
// Passive effects run in the wheel firmware: stable at any update rate.
// Friction models tyre scrub, strongest when parked and fading by about 30 km/h.
inline ConditionLevels conditionLevels(float speedMS, const ForceSetup& f) {
    const float low = clamp(1 - std::fabs(speedMS) / 8, 0, 1);
    ConditionLevels c;
    c.damper = clamp(f.damping, 0, 1);
    c.friction = clamp(f.friction, 0, 1) * (.3f + .7f * low);
    return c;
}
struct ForceShaper {
    float filtered = 0;
    void reset() { filtered = 0; }
    ForceReport step(const ForceFrame& in, const ForceSetup& f) {
        filtered += (in.torque - filtered) * clamp(f.filter, .05f, 1);
        const float raw = clamp(f.gain, 0, 1.5f) * (filtered + in.road * clamp(f.road, 0, 1));
        ForceReport r;
        r.clipping = std::fabs(raw) > 1;
        r.output = softClip(raw, f.knee);
        return r;
    }
};
}
