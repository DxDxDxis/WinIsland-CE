#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace wi {
// Presentation only. These values never issue a command or mutate media state.
struct MusicControlRect { float x, y, w, h; };
inline std::array<MusicControlRect, 4> musicControlLayout(float width, bool mode) {
    const float usable = std::max(0.f, width - 24.f);
    const float slots = mode ? 5.f : 3.f;
    const float gap = std::min(8.f, usable / (slots * 6.f));
    const float size = std::min(42.f, std::max(0.f, (usable - gap * (slots - 1)) / slots));
    const float step = size + gap, center = width / 2;
    std::array<MusicControlRect, 4> result{};
    for (int i = 0; i < 4; ++i)
        result[i] = {center + (i - 1) * step - size / 2, 0, size, 30};
    return result;
}

struct MusicControlMotion {
    double pause = 0, known = 0, visibility = 0;
    std::array<double, 4> hover{}, press{}, focus{}, enabled{};
    bool initialized = false;

    // Same exponential response as the host feedback loop. Retargeting starts
    // from the current presentation value; no queued animations or timers.
    bool advance(double dt, bool reduce, bool visible, bool playing, bool paused,
                 bool loading, const std::array<bool, 4>& available, bool busy,
                 int hovered, int pressed, int focused) {
        bool changed = false;
        auto approach = [&](double& value, double goal, double speed) {
            double old = value;
            value = reduce ? goal : goal + (value - goal) * std::exp(-std::clamp(dt, 0., .1) * speed);
            if (std::abs(value - goal) < .001) value = goal;
            changed |= value != old;
        };
        const bool stateKnown = !loading && (playing || paused);
        if (!initialized) { pause = playing ? 1 : 0; known = stateKnown ? 1 : 0; initialized = true; }
        if (stateKnown) approach(pause, playing ? 1 : 0, 28);
        approach(known, stateKnown ? 1 : 0, 32);
        approach(visibility, visible ? 1 : 0, 26);
        for (int i = 0; i < 4; ++i) {
            const bool active = visible && available[i] && !busy;
            approach(hover[i], active && hovered == 101 + i ? 1 : 0, 32);
            approach(press[i], active && pressed == 101 + i ? 1 : 0, 42);
            approach(focus[i], visible && focused == 101 + i ? 1 : 0, 32);
            approach(enabled[i], available[i] && !busy ? 1 : 0, 32);
        }
        return changed;
    }
};

struct MusicIconPoint { float x, y; };
inline std::array<MusicIconPoint, 4> musicPlayContour(double pause) {
    constexpr std::array<MusicIconPoint, 4> play{{{-5,-8},{10,-.65f},{10,.65f},{-5,8}}};
    constexpr std::array<MusicIconPoint, 4> bar{{{-6,-8},{-1.5f,-8},{-1.5f,8},{-6,8}}};
    std::array<MusicIconPoint, 4> result{};
    const float t = (float)std::clamp(pause, 0., 1.);
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = {play[i].x + (bar[i].x - play[i].x) * t,
                     play[i].y + (bar[i].y - play[i].y) * t};
    return result;
}
} // namespace wi
