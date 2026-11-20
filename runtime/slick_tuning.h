#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace slick {
enum class Preset { stock, basic, modern, w11 };

inline constexpr Preset parse_preset(std::string_view mode) {
    if (mode == "w11") return Preset::w11;
    if (mode == "2026" || mode == "handling") return Preset::modern;
    if (mode == "1") return Preset::basic;
    return Preset::stock;
}

inline Preset selected_preset() {
    static const Preset preset = [] {
        const char* mode = std::getenv("F1CE_SLICK_TEST");
        return parse_preset(mode ? mode : "");
    }();
    return preset;
}

inline bool handling_enabled() { return selected_preset() == Preset::modern || selected_preset() == Preset::w11; }

inline float pad_steer(float x, float kmh) {
    const float fast = std::clamp((kmh - 90.0f) / 150.0f, 0.0f, 1.0f);
    const float small = 0.82f - 0.12f * fast;
    return x * (small + (1.0f - small) * x * x) * (1.0f - 0.22f * fast);
}

struct AxleGrip { float front, rear; };

inline float smoothstep(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline AxleGrip modern_grip(float kmh) {
    const float speed = std::clamp(kmh, 0.0f, 240.0f);
    const float aero = 0.06f * speed * speed / (240.0f * 240.0f);
    const float balance = smoothstep((speed - 20.0f) / 180.0f);
    const float base = 1.10f + aero;
    return {base * (1.0f + 0.015f * (1.0f - balance)), base * (1.0f + 0.015f * balance)};
}

inline AxleGrip w11_grip(float kmh) {
    const float speed = std::clamp(kmh, 0.0f, 280.0f);
    const float aero = 0.12f * speed * speed / (240.0f * 240.0f);
    const float balance = smoothstep((speed - 65.0f) / 150.0f);
    const float base = 1.07f + aero;
    return {base * (1.0f + 0.012f * (1.0f - balance)), base * (1.0f + 0.020f * balance)};
}

inline float w11_pad_steer(float x, float kmh) {
    const float high_speed = smoothstep((kmh - 120.0f) / 140.0f);
    const float response = x * (0.90f + 0.10f * x * x);
    return response * (1.0f - 0.18f * high_speed);
}

inline float drive_boost_ceiling(Preset preset, float speed_kmh, float throttle) {
    if (preset != Preset::modern && preset != Preset::w11) return 0.0f;
    const float low = preset == Preset::w11 ? 12.0f : 15.0f;
    const float end = preset == Preset::w11 ? 320.0f : 280.0f;
    const float ramp = std::min(std::clamp((speed_kmh - low) / 45.0f, 0.0f, 1.0f),
                                std::clamp((end - speed_kmh) / (preset == Preset::w11 ? 120.0f : 100.0f), 0.0f, 1.0f));
    return (preset == Preset::w11 ? 0.08f : 0.12f) * ramp *
           std::clamp((throttle - (preset == Preset::w11 ? 0.40f : 0.45f)) /
                      (preset == Preset::w11 ? 0.60f : 0.55f), 0.0f, 1.0f);
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
bool telemetry_enabled();
void telemetry_pad_axis(unsigned lx);
bool physics_eligible(unsigned car);
}
