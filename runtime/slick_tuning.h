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

// One update per stock physics substep (three per 60 Hz simulation tick).
// Cut extra torque in ~45 ms, rebuild it in ~180 ms. Never retain boost above
// the current throttle/speed ceiling, and immediately drop it when ineligible.
struct DriveResponse {
    float boost = 0.0f;

    float step(float ceiling, bool slipping) {
        constexpr float max_boost = 0.12f;
        constexpr float hz = 180.0f;
        const float target = slipping ? 0.0f : ceiling;
        const float delta = target - boost;
        boost += std::clamp(delta, -max_boost / (0.045f * hz), max_boost / (0.180f * hz));
        return boost = std::clamp(boost, 0.0f, ceiling);
    }
};

enum class Compound { baseline, soft, hard };

inline Compound selected_compound() {
    static const Compound value = [] {
        const char* name = std::getenv("F1CE_SLICK_COMPOUND");
        if (name && std::strcmp(name, "soft") == 0) return Compound::soft;
        if (name && std::strcmp(name, "hard") == 0) return Compound::hard;
        return Compound::baseline;
    }();
    return value;
}

inline float wear_multiplier(Compound compound) {
    return compound == Compound::soft ? 1.55f : compound == Compound::hard ? 0.90f : 1.25f;
}

inline float compound_grip(Compound compound, float wear) {
    // Wear is the observed internal per-wheel value, not a percentage.
    const float used = wear > 0.0f ? std::min(wear / 0.65f, 1.0f) : 0.0f;
    if (compound == Compound::soft) return 1.04f - 0.12f * used;
    if (compound == Compound::hard) return 0.98f - 0.015f * used;
    return 1.0f;
}

// A negative value means the player controller is not currently updating.
float player_speed_kmh();
}
