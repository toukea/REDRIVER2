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
        out.drive = out.gear && !out.stalled ? in.throttle * engagement * torqueCurve * cfg.ratios[idx] / cfg.ratios[1] * revLimit : 0;
        if (out.gear < 0) out.drive = -out.drive;
        out.engineBrake = out.gear ? engagement * (1-in.throttle) * .12f * cfg.ratios[idx] / cfg.ratios[1] : 0;
        out.burnout = cfg.burnout && out.gear == 1 && in.throttle > .97f && engagement > .35f && !out.stalled && std::fabs(speedMS) < 8;
        if (out.burnout) out.rpm = std::max(out.rpm, 3500.f);
        return out;
    }
};
}
