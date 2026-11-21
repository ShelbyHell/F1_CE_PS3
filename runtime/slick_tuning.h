#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace slick {

// ============================================================================
// Guest Vehicle Memory Layout Offsets
// ============================================================================
// Offsets within the guest PS3 vehicle simulation object (0x1390 bytes).
namespace CarOffset {
    constexpr unsigned FwdVector    = 0x0d0; // 3x float: body forward direction vector [X, Y, Z]
    constexpr unsigned Position     = 0x0e0; // 3x float: body world position vector [X, Y, Z]
    constexpr unsigned BodyPtr      = 0x140; // u32: pointer to vehicle rigid body
    constexpr unsigned Speed        = 0x3c0; // float: vehicle speed in m/s (speed * 3.6f -> km/h)
    constexpr unsigned TyreForces   = 0x4d0; // 4x float: tyre force outputs
    constexpr unsigned WheelLoads   = 0x530; // 4x float: vertical loads per wheel [FL, FR, RL, RR]
    constexpr unsigned GripLevel    = 0x554; // float: tyre grip ratio
    constexpr unsigned TyreFlags    = 0x5f0; // u32: surface and tyre configuration flags
    constexpr unsigned TyreWear     = 0x8b0; // 4x float: accumulated tyre wear [FL, FR, RL, RR]
    constexpr unsigned EngineOmega  = 0x94c; // float: engine speed in rad/s
    constexpr unsigned Throttle     = 0x950; // float: driver throttle demand (0.0 to 1.0)
    constexpr unsigned WearMode     = 0x964; // u32: tyre wear simulation mode (1 = Career, 2 = Quick Race)
    constexpr unsigned WheelSlip    = 0x97c; // u32: slip bitmask (bits 8..11 indicate slip on FL, FR, RL, RR)
    constexpr unsigned WheelContact = 0x980; // u32: ground contact bitmask (bits 0..3: FL, FR, RL, RR)
    constexpr unsigned Gear         = 0x9b8; // s8: engaged gear (-1 = Reverse, 0 = Neutral, 1..7 = Forward)
}

// ============================================================================
// Physics Presets & Gamepad Steering Response
// ============================================================================
enum class Preset { stock, basic, modern, w11 };

inline constexpr Preset parse_preset(std::string_view mode) {
    if (mode == "w11") return Preset::w11;
    if (mode == "2026" || mode == "handling" || mode == "modern") return Preset::modern;
    if (mode == "1" || mode == "basic") return Preset::basic;
    return Preset::stock;
}

inline Preset selected_preset() {
    static const Preset preset = [] {
        const char* mode = std::getenv("F1CE_HANDLING");
        if (!mode || !*mode) mode = std::getenv("F1CE_SLICK_TEST");
        return parse_preset(mode ? mode : "");
    }();
    return preset;
}

inline bool handling_enabled() {
    return selected_preset() == Preset::modern || selected_preset() == Preset::w11;
}

inline float smoothstep(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 2026 Modern Preset: progressive steering filter
inline float pad_steer(float x, float kmh) {
    const float fast = std::clamp((kmh - 90.0f) / 150.0f, 0.0f, 1.0f);
    const float small = 0.82f - 0.12f * fast;
    return x * (small + (1.0f - small) * x * x) * (1.0f - 0.22f * fast);
}

// 2020 W11 Profile: high-downforce, razor-sharp steering curve
inline float w11_pad_steer(float x, float kmh) {
    const float high_speed = smoothstep((kmh - 120.0f) / 140.0f);
    const float response = x * (0.90f + 0.10f * x * x);
    return response * (1.0f - 0.18f * high_speed);
}

// ============================================================================
// Axle Grip Curves & Drive Torque Boost
// ============================================================================
struct AxleGrip {
    float front = 1.0f;
    float rear  = 1.0f;
};

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

// Smooth rate limiter for torque augmentation
// One update per stock physics substep (three per 60 Hz simulation tick).
// Cut extra torque in ~45 ms, rebuild it in ~180 ms.
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

// ============================================================================
// Tyre Compounds & Degradation Model
// ============================================================================
enum class Compound { baseline, soft, hard };

inline Compound selected_compound() {
    static const Compound value = [] {
        const char* name = std::getenv("F1CE_TYRE_COMPOUND");
        if (!name || !*name) name = std::getenv("F1CE_SLICK_COMPOUND");
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

inline bool dry_wear_state(unsigned tyre_flags, unsigned wear_mode) {
    // Career uses wear mode 1; Quick Race uses 2. 0x3000 is a transient
    // session/status field, while the low 5 bits identify the dry surface.
    return (tyre_flags & 0x1fu) == 0x19u && (wear_mode == 1u || wear_mode == 2u);
}

// ============================================================================
// Dynamic Tyre Thermodynamics (Surface / Carcass & Thermal Grip Window)
// ============================================================================
struct ThermalWindow {
    float opt_min = 85.0f;
    float opt_max = 105.0f;
    float peak    = 95.0f;
};

inline constexpr ThermalWindow compound_thermal_window(Compound c) {
    switch (c) {
        case Compound::soft:
            return {85.0f, 105.0f, 95.0f};
        case Compound::hard:
            return {95.0f, 115.0f, 105.0f};
        case Compound::baseline:
        default:
            return {80.0f, 102.0f, 90.0f};
    }
}

inline float thermal_grip_factor(Compound c, float temp) {
    const auto win = compound_thermal_window(c);
    if (temp >= win.opt_min && temp <= win.opt_max) {
        return 1.0f;
    }
    if (temp < win.opt_min) {
        const float cold_range = std::max(win.opt_min - 25.0f, 10.0f);
        const float ratio = std::clamp((win.opt_min - temp) / cold_range, 0.0f, 1.0f);
        return 1.0f - 0.16f * ratio * ratio;
    }
    // Overheated degradation
    const float hot_range = 35.0f;
    const float ratio = std::clamp((temp - win.opt_max) / hot_range, 0.0f, 2.0f);
    return std::max(1.0f - 0.18f * ratio * ratio, 0.70f);
}

inline float thermal_wear_factor(Compound c, float temp) {
    const auto win = compound_thermal_window(c);
    if (temp <= win.opt_max) {
        return 1.0f;
    }
    const float excess = (temp - win.opt_max) / 25.0f;
    return std::min(1.0f + 1.5f * excess * excess, 3.5f);
}

struct WheelThermal {
    float surface = 85.0f;
    float carcass = 85.0f;

    float temp() const { return 0.70f * surface + 0.30f * carcass; }

    void step(float dt, float speed_mps, float load_n, bool slipping,
              float steer_angle, float throttle, bool is_front, Compound comp) {
        constexpr float T_ambient = 28.0f;

        const float v = std::max(speed_mps, 0.0f);
        const float load_norm = std::clamp(load_n / 3500.0f, 0.0f, 2.5f);

        // Heat generation multipliers based on compound
        const float comp_heat_mult = (comp == Compound::soft) ? 1.15f : (comp == Compound::hard) ? 0.90f : 1.0f;

        // 1. Rolling deformation / carcass flexure heating
        const float roll_heat = 0.35f * (v / 50.0f) * load_norm * comp_heat_mult;

        // 2. Surface normal rolling friction & steering scrub
        const float normal_scrub = 0.25f * (v / 50.0f) * load_norm * comp_heat_mult;
        const float lat_scrub = is_front ? (0.065f * std::fabs(steer_angle) * v * load_norm * comp_heat_mult)
                                         : (0.025f * std::fabs(steer_angle) * v * load_norm * comp_heat_mult);

        // 3. Longitudinal slip (wheelspin on throttle / lockup on braking)
        float slip_heat = 0.0f;
        if (slipping) {
            if (!is_front && throttle > 0.25f) {
                // Wheelspin under power
                slip_heat = 12.0f * throttle * (1.0f + v / 40.0f) * comp_heat_mult;
            } else {
                // Heavy braking lockup or cornering slide
                slip_heat = 14.0f * std::clamp(v / 25.0f, 0.2f, 3.0f) * comp_heat_mult;
            }
        }

        const float heat_surface = normal_scrub + lat_scrub + slip_heat;

        // 4. Convective airflow cooling
        const float h_surface = 0.0022f + 0.000085f * v;
        const float cool_surface = h_surface * (surface - T_ambient);

        const float h_carcass = 0.0008f + 0.000025f * v;
        const float cool_carcass = h_carcass * (carcass - T_ambient);

        // 5. Thermal conduction between surface and carcass
        const float transfer = 1.6f * (surface - carcass);

        // Integrate
        surface += (heat_surface - cool_surface - transfer) * dt;
        carcass += (roll_heat - cool_carcass + 0.45f * transfer) * dt;

        surface = std::clamp(surface, T_ambient, 160.0f);
        carcass = std::clamp(carcass, T_ambient, 160.0f);
    }
};

struct CarThermalState {
    WheelThermal wheels[4];
    uint64_t last_update_us = 0;
    bool initialized = false;

    void init(Compound comp) {
        const auto win = compound_thermal_window(comp);
        for (int w = 0; w < 4; ++w) {
            wheels[w].surface = win.peak - 5.0f;
            wheels[w].carcass = win.peak - 5.0f;
        }
        initialized = true;
    }
};

struct RgbaColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

inline RgbaColor thermochromic_color(float temp, Compound compound) {
    const auto win = compound_thermal_window(compound);

    auto lerp_color = [](int r1, int g1, int b1, int r2, int g2, int b2, float t) -> RgbaColor {
        t = std::clamp(t, 0.0f, 1.0f);
        return {
            static_cast<uint8_t>(r1 + (r2 - r1) * t),
            static_cast<uint8_t>(g1 + (g2 - g1) * t),
            static_cast<uint8_t>(b1 + (b2 - b1) * t),
            240
        };
    };

    if (temp < win.opt_min) {
        const float t = std::clamp((temp - (win.opt_min - 20.0f)) / 20.0f, 0.0f, 1.0f);
        if (t < 0.5f) {
            return lerp_color(30, 90, 220, 0, 175, 215, t / 0.5f);
        } else {
            return lerp_color(0, 175, 215, 0, 195, 105, (t - 0.5f) / 0.5f);
        }
    }
    if (temp <= win.opt_max) {
        return {0, 195, 105, 240}; // Pure Siemens Green
    }
    // Overheated
    const float t = std::clamp((temp - win.opt_max) / 20.0f, 0.0f, 1.0f);
    if (t < 0.5f) {
        return lerp_color(0, 195, 105, 245, 155, 20, t / 0.5f);
    } else {
        return lerp_color(245, 155, 20, 230, 40, 40, (t - 0.5f) / 0.5f);
    }
}

struct PlayerTyreTelemetry {
    std::atomic<float> temps[4];
    std::atomic<float> surface_temps[4];
    std::atomic<float> carcass_temps[4];

    PlayerTyreTelemetry() {
        for (int i = 0; i < 4; ++i) {
            temps[i].store(85.0f, std::memory_order_relaxed);
            surface_temps[i].store(85.0f, std::memory_order_relaxed);
            carcass_temps[i].store(85.0f, std::memory_order_relaxed);
        }
    }
};

inline PlayerTyreTelemetry& player_tyre_telemetry() {
    static PlayerTyreTelemetry telem;
    return telem;
}

bool physics_eligible(unsigned car);

// ============================================================================
// Braking Dynamics & Dynamic Trail-Braking
// ============================================================================
inline float selected_brake_bias() {
    static const float bias = [] {
        const char* val = std::getenv("F1CE_BRAKE_BIAS");
        if (val) {
            float b = (float)std::atof(val);
            if (b >= 50.0f && b <= 65.0f) return b * 0.01f;
        }
        return 0.56f;
    }();
    return bias;
}

struct TrailBrakeEffect {
    float fl_mult = 1.0f;
    float fr_mult = 1.0f;
    float rl_mult = 1.0f;
    float rr_mult = 1.0f;
};

inline TrailBrakeEffect trail_brake_grip(float speed_kmh, float pad_steer,
                                         float load_0, float load_1, float load_2, float load_3,
                                         float brake_bias = 0.56f) {
    TrailBrakeEffect res;
    if (speed_kmh < 30.0f) return res;

    const float front_load = load_0 + load_1;
    const float rear_load = load_2 + load_3;
    const float total_load = front_load + rear_load;
    if (total_load < 500.0f) return res;

    const float front_ratio = front_load / total_load;
    if (front_ratio < 0.52f) return res;

    const float steer_mag = std::fabs(pad_steer);
    if (steer_mag < 0.08f) return res;

    const float brake_intensity = std::clamp((front_ratio - 0.52f) / 0.16f, 0.0f, 1.0f);
    const float trail_blend = brake_intensity * std::clamp(steer_mag / 0.60f, 0.0f, 1.0f);

    const bool turning_left = pad_steer < 0.0f;
    const float load_inside = turning_left ? load_0 : load_1;
    const float inside_ratio = front_load > 100.0f ? (load_inside / front_load) : 0.5f;

    const float bias_factor = (brake_bias - 0.56f) * 2.5f;

    float inside_scrub = 0.0f;
    if (inside_ratio < 0.35f) {
        inside_scrub = trail_blend * (0.35f - inside_ratio) * 0.35f * (1.0f + bias_factor);
    }

    const float outside_bite = trail_blend * 0.025f * std::max(1.0f - inside_scrub * 2.0f, 0.0f);
    const float rear_rotation = trail_blend * std::max(0.018f - bias_factor * 0.02f, 0.005f);

    if (turning_left) {
        res.fl_mult -= inside_scrub;
        res.fr_mult += outside_bite;
        res.rl_mult -= rear_rotation;
        res.rr_mult -= rear_rotation * 0.5f;
    } else {
        res.fr_mult -= inside_scrub;
        res.fl_mult += outside_bite;
        res.rr_mult -= rear_rotation;
        res.rl_mult -= rear_rotation * 0.5f;
    }
    return res;
}

// ============================================================================
// Aerodynamics: Dirty Air & Slipstream Model
// ============================================================================
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct WakeInfo {
    float dist_ahead = 999.0f;
    float lateral_offset = 0.0f;
    float intensity = 0.0f;
    bool in_slipstream = false;
};

inline WakeInfo evaluate_wake(Vec3 pos, Vec3 fwd, const Vec3* others, size_t count, float speed_kmh, float steer_abs) {
    WakeInfo best;
    const float fwd_len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fwd_len < 1e-4f) return best;
    fwd.x /= fwd_len; fwd.y /= fwd_len; fwd.z /= fwd_len;

    for (size_t i = 0; i < count; ++i) {
        const Vec3 d{others[i].x - pos.x, others[i].y - pos.y, others[i].z - pos.z};
        const float dist_fwd = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;
        if (dist_fwd < 2.5f || dist_fwd > 65.0f) continue;

        const Vec3 perp{d.x - dist_fwd * fwd.x, d.y - dist_fwd * fwd.y, d.z - dist_fwd * fwd.z};
        const float cone_radius = 2.2f + 0.08f * dist_fwd;
        const float lat_dist_sq = perp.x * perp.x + perp.y * perp.y + perp.z * perp.z;
        if (lat_dist_sq > cone_radius * cone_radius) continue;

        const float lat_dist = std::sqrt(lat_dist_sq);

        const float long_fade = (65.0f - dist_fwd) / 62.5f;
        const float lat_fade = (cone_radius - lat_dist) / cone_radius;
        const float intensity = std::clamp(long_fade * lat_fade, 0.0f, 1.0f);
        if (intensity > best.intensity) {
            best.dist_ahead = dist_fwd;
            best.lateral_offset = lat_dist;
            best.intensity = intensity;
        }
    }

    if (best.intensity > 0.15f && speed_kmh > 160.0f && steer_abs < 0.20f) {
        best.in_slipstream = true;
    }
    return best;
}

inline AxleGrip dirty_air_penalty(WakeInfo wake, float speed_kmh) {
    if (wake.intensity <= 0.0f) return {0.0f, 0.0f};
    const float aero = std::clamp((speed_kmh - 80.0f) / 130.0f, 0.0f, 1.0f);
    // Front wing wash-out creates understeer in high-speed bends; rear loses less
    return {0.065f * wake.intensity * aero, 0.025f * wake.intensity * aero};
}

inline float slipstream_boost(WakeInfo wake, float speed_kmh, float throttle) {
    if (!wake.in_slipstream || throttle < 0.85f) return 0.0f;
    const float speed_factor = std::clamp((speed_kmh - 160.0f) / 80.0f, 0.0f, 1.0f);
    return 0.06f * wake.intensity * speed_factor;
}

inline bool dirty_air_enabled() {
    static const bool enabled = [] {
        const char* val = std::getenv("F1CE_DIRTY_AIR");
        return !val || std::strcmp(val, "0") != 0;
    }();
    return enabled && handling_enabled();
}

struct WakeState {
    std::atomic<float> dist_ahead{999.0f};
    std::atomic<float> lateral_offset{0.0f};
    std::atomic<float> intensity{0.0f};
    std::atomic<bool> in_slipstream{false};
};

inline WakeState& wake_state() {
    static WakeState state;
    return state;
}

inline void update_wake(const WakeInfo& w) {
    auto& s = wake_state();
    s.dist_ahead.store(w.dist_ahead, std::memory_order_relaxed);
    s.lateral_offset.store(w.lateral_offset, std::memory_order_relaxed);
    s.intensity.store(w.intensity, std::memory_order_relaxed);
    s.in_slipstream.store(w.in_slipstream, std::memory_order_relaxed);
}

inline WakeInfo current_wake() {
    auto& s = wake_state();
    return {s.dist_ahead.load(std::memory_order_relaxed),
            s.lateral_offset.load(std::memory_order_relaxed),
            s.intensity.load(std::memory_order_relaxed),
            s.in_slipstream.load(std::memory_order_relaxed)};
}

// ============================================================================
// Runtime Telemetry & In-Game HUD State
// ============================================================================
inline std::atomic<unsigned>& player_car_ref() {
    static std::atomic<unsigned> car{0};
    return car;
}

inline void set_player_car(unsigned car) { player_car_ref().store(car, std::memory_order_relaxed); }
inline unsigned player_car() { return player_car_ref().load(std::memory_order_relaxed); }

// A negative value means the player controller is not currently updating.
float player_speed_kmh();
float player_pad_steer();
bool telemetry_enabled();
void telemetry_pad_axis(unsigned lx);

inline bool tyre_hud_enabled() {
    static const bool enabled = [] {
        const char* val = std::getenv("F1CE_TYRE_HUD");
        return val && std::strcmp(val, "1") == 0;
    }();
    return enabled;
}

} // namespace slick
