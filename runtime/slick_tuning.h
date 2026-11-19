#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace slick {
inline bool handling_enabled() {
    static const bool enabled = [] {
        const char* mode = std::getenv("F1CE_SLICK_TEST");
        return mode && std::strcmp(mode, "handling") == 0;
    }();
    return enabled;
}

inline float pad_steer(float x, float kmh) {
    const float fast = std::clamp((kmh - 90.0f) / 150.0f, 0.0f, 1.0f);
    const float small = 0.82f - 0.12f * fast;
    return x * (small + (1.0f - small) * x * x) * (1.0f - 0.22f * fast);
}

// A negative value means the player controller is not currently updating.
float player_speed_kmh();
}
