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

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct WakeInfo {
    float dist_ahead = 999.0f;
    float lateral_offset = 0.0f;
    float intensity = 0.0f;
    bool in_slipstream = false;
};

struct RgbaColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct BrakeDiscThermal {
    float temp = 450.0f; // Carbon-Carbon disc temp in °C

    void step(float dt, float speed_mps, float brake_demand, float axle_bias, float load_n) {
        constexpr float T_amb = 30.0f;
        const float v = std::max(speed_mps, 0.0f);
        const float load_norm = std::clamp(load_n / 3500.0f, 0.2f, 2.0f);

        // Heat generation under braking: P = k * brake * speed * load * bias
        float heat = 0.0f;
        if (brake_demand > 0.02f) {
            heat = 420.0f * brake_demand * (v / 50.0f) * load_norm * (axle_bias / 0.5f);
        }

        // Forced duct cooling proportional to speed
        const float h_duct = 0.0035f + 0.00018f * v;
        const float cool_duct = h_duct * (temp - T_amb);

        // Stefan-Boltzmann radiation cooling: emissive at high temp
        const float t_k = (temp + 273.15f) * 0.001f;
        const float t_amb_k = (T_amb + 273.15f) * 0.001f;
        const float cool_rad = 0.045f * (t_k * t_k * t_k * t_k - t_amb_k * t_amb_k * t_amb_k * t_amb_k);

        temp += (heat - cool_duct - cool_rad) * dt;
        temp = std::clamp(temp, T_amb, 1250.0f);
    }
};

inline float brake_fade_factor(float temp) {
    if (temp < 350.0f) {
        // Cold bite
        return 0.85f + 0.15f * std::max((temp - 30.0f) / 320.0f, 0.0f);
    }
    if (temp <= 850.0f) {
        return 1.0f; // Optimal operating window
    }
    if (temp <= 1050.0f) {
        // Progressive fade
        return 1.0f - 0.15f * ((temp - 850.0f) / 200.0f);
    }
    // Severe overheat fade
    const float excess = std::min((temp - 1050.0f) / 150.0f, 1.0f);
    return std::max(0.85f - 0.15f * excess, 0.70f);
}

inline RgbaColor incandescent_brake_color(float temp) {
    // Returns glowing incandescent color for brake discs
    if (temp < 400.0f) {
        return {80, 95, 110, 220}; // Cold steel slate
    }
    if (temp < 600.0f) {
        // Transition: slate to dull red
        const float t = (temp - 400.0f) / 200.0f;
        return {
            static_cast<uint8_t>(80 + (170 - 80) * t),
            static_cast<uint8_t>(95 + (45 - 95) * t),
            static_cast<uint8_t>(110 + (25 - 110) * t),
            230
        };
    }
    if (temp < 850.0f) {
        // Dull red to glowing orange
        const float t = (temp - 600.0f) / 250.0f;
        return {
            static_cast<uint8_t>(170 + (255 - 170) * t),
            static_cast<uint8_t>(45 + (130 - 45) * t),
            static_cast<uint8_t>(25 + (20 - 25) * t),
            245
        };
    }
    if (temp < 1050.0f) {
        // Glowing orange to incandescent yellow
        const float t = (temp - 850.0f) / 200.0f;
        return {
            255,
            static_cast<uint8_t>(130 + (210 - 130) * t),
            static_cast<uint8_t>(20 + (50 - 20) * t),
            255
        };
    }
    // > 1050 °C: White-hot incandescent
    const float t = std::clamp((temp - 1050.0f) / 150.0f, 0.0f, 1.0f);
    return {
        255,
        static_cast<uint8_t>(210 + (250 - 210) * t),
        static_cast<uint8_t>(50 + (210 - 50) * t),
        255
    };
}

struct CarThermalState {
    WheelThermal wheels[4];
    BrakeDiscThermal brakes[4];
    uint64_t last_update_us = 0;
    bool initialized = false;

    void init(Compound comp) {
        const auto win = compound_thermal_window(comp);
        for (int w = 0; w < 4; ++w) {
            wheels[w].surface = win.peak - 5.0f;
            wheels[w].carcass = win.peak - 5.0f;
            brakes[w].temp = 480.0f;
        }
        initialized = true;
    }
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
    std::atomic<float> brake_temps[4];
    std::atomic<float> throttle{0.0f};
    std::atomic<float> brake{0.0f};
    std::atomic<float> g_lat{0.0f};
    std::atomic<float> g_long{0.0f};
    std::atomic<int> drs_state{0}; // 0 = Closed, 1 = Available, 2 = Open

    PlayerTyreTelemetry() {
        for (int i = 0; i < 4; ++i) {
            temps[i].store(85.0f, std::memory_order_relaxed);
            surface_temps[i].store(85.0f, std::memory_order_relaxed);
            carcass_temps[i].store(85.0f, std::memory_order_relaxed);
            brake_temps[i].store(450.0f, std::memory_order_relaxed);
        }
    }
};

inline PlayerTyreTelemetry& player_tyre_telemetry() {
    static PlayerTyreTelemetry telem;
    return telem;
}

// ============================================================================
// Session Mode Tracking (Time Trial / Test vs Grand Prix / Race)
// ============================================================================
inline std::atomic<bool>& time_trial_session_flag() {
    static std::atomic<bool> flag{false};
    return flag;
}

inline void set_time_trial_session(bool is_tt) {
    time_trial_session_flag().store(is_tt, std::memory_order_relaxed);
}

inline bool is_time_trial_session() {
    return time_trial_session_flag().load(std::memory_order_relaxed);
}

// ============================================================================
// Drag Reduction System (DRS) & Dynamic Flap Controller
// ============================================================================
enum class DrsState : int {
    Closed = 0,
    Available = 1,
    Open = 2,
    Armed = 3
};

struct DrsController {
    DrsState state = DrsState::Closed;
    bool btn_latched = false;
    bool armed = false;
    float corner_time = 0.0f;
    float straight_time = 0.0f;
    float straight_dist = 0.0f;
    float cooldown_time = 0.0f;

    void update(float dt, float speed_kmh, float throttle, float steer, float brake,
                const WakeInfo& wake, bool btn_drs, bool auto_drs, bool enabled, bool is_time_trial = false) {
        if (!enabled) {
            state = DrsState::Closed;
            armed = false;
            return;
        }

        if (cooldown_time > 0.0f) {
            cooldown_time -= dt;
        }

        static const bool drs_always = []{
            const char* e = std::getenv("F1CE_DRS");
            return e && std::strcmp(e, "always") == 0;
        }();

        const bool trailing_rival = (wake.in_slipstream || wake.dist_ahead < 70.0f || wake.intensity > 0.02f);
        const bool eligible_at_detect = (is_time_trial || trailing_rival || drs_always);

        const bool is_cornering = (std::fabs(steer) > 0.18f);
        const bool is_straight = (std::fabs(steer) < 0.14f && throttle > 0.70f);

        if (is_cornering) {
            corner_time += dt;
            straight_time = 0.0f;
            straight_dist = 0.0f;
        } else if (is_straight) {
            straight_time += dt;
            straight_dist += (speed_kmh / 3.6f) * dt;
        }

        // 1. Detection Point: Triggered upon unwinding steering at corner exit onto a straight.
        // Requires prior cornering (>0.5s) to guarantee a real turn before the straight,
        // or session start high-speed rollout, with cooldown elapsed.
        const bool corner_exit_trigger = (corner_time > 0.50f && !is_cornering && throttle > 0.60f);
        const bool flying_start_trigger = (!armed && speed_kmh > 170.0f && straight_dist > 45.0f);

        if ((corner_exit_trigger || flying_start_trigger) && cooldown_time <= 0.0f && !armed && state == DrsState::Closed) {
            corner_time = 0.0f;
            if (eligible_at_detect) {
                armed = true;
                state = DrsState::Armed;
            }
        }

        // 2. Straight DRS Activation Zone: Car accelerates down the straight
        const bool in_activation_zone = (straight_time > 0.50f && straight_dist > 45.0f && speed_kmh > 175.0f && std::fabs(steer) < 0.16f && throttle > 0.75f);

        if (state == DrsState::Open) {
            // End of straight / braking zone: Instant cancellation & disarm until next corner detection
            if (brake > 0.08f || std::fabs(steer) > 0.22f || throttle < 0.40f || speed_kmh < 150.0f) {
                state = DrsState::Closed;
                armed = false;
                cooldown_time = 3.5f;
            }
        } else if (in_activation_zone && armed) {
            // Reached DRS zone on straight with valid authorization
            if (auto_drs || (btn_drs && !btn_latched)) {
                state = DrsState::Open;
            } else {
                state = DrsState::Available;
            }
        } else if (state == DrsState::Armed) {
            // Remains Armed while transitioning from corner exit to the activation line
            if (brake > 0.08f || std::fabs(steer) > 0.25f) {
                state = DrsState::Closed;
                armed = false;
                cooldown_time = 2.0f;
            }
        } else if (state == DrsState::Available) {
            // In DRS zone on straight, waiting for manual button press
            if (brake > 0.08f || std::fabs(steer) > 0.22f || throttle < 0.40f || speed_kmh < 150.0f) {
                state = DrsState::Closed;
                armed = false;
                cooldown_time = 3.5f;
            } else if (btn_drs && !btn_latched) {
                state = DrsState::Open;
            }
        } else {
            state = DrsState::Closed;
        }

        btn_latched = btn_drs;
    }
};

inline DrsController& global_drs_controller() {
    static DrsController ctrl;
    return ctrl;
}

inline DrsState current_drs_state() {
    return (DrsState)player_tyre_telemetry().drs_state.load(std::memory_order_relaxed);
}

inline bool drs_enabled() {
    static const bool enabled = []{
        const char* e = std::getenv("F1CE_DRS");
        if (e && std::strcmp(e, "0") == 0) return false;
        return true;
    }();
    return enabled;
}

inline bool drs_auto_mode() {
    static const bool autom = []{
        const char* e = std::getenv("F1CE_DRS");
        return e && std::strcmp(e, "auto") == 0;
    }();
    return autom;
}

// ============================================================================
// Driver Pad Inputs (Brake, Throttle, Buttons)
// ============================================================================
struct PadInputs {
    std::atomic<float> throttle{0.0f};
    std::atomic<float> brake{0.0f};
    std::atomic<bool> btn_triangle{false};
};

inline PadInputs& player_pad_inputs() {
    static PadInputs inputs;
    return inputs;
}

// ============================================================================
// G-Force Vector Tracker & Filter
// ============================================================================
struct GForceTracker {
    float g_lat = 0.0f;
    float g_long = 0.0f;
    float last_speed_mps = 0.0f;
    Vec3 last_fwd{0.0f, 0.0f, 1.0f};

    void update(float dt, float speed_mps, Vec3 fwd) {
        if (dt < 0.0001f) return;
        const float alpha = std::clamp(dt / 0.075f, 0.05f, 1.0f);

        // Longitudinal G: delta velocity
        const float a_long = (speed_mps - last_speed_mps) / dt / 9.80665f;
        g_long += alpha * (a_long - g_long);
        last_speed_mps = speed_mps;

        const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
        if (len > 1e-4f) {
            fwd.x /= len; fwd.y /= len; fwd.z /= len;
            const float dot = std::clamp(fwd.x * last_fwd.x + fwd.y * last_fwd.y + fwd.z * last_fwd.z, -1.0f, 1.0f);
            const float angle = std::acos(dot);
            const float cross_y = fwd.z * last_fwd.x - fwd.x * last_fwd.z;
            const float dir = cross_y > 0.0f ? 1.0f : -1.0f;
            const float a_lat = dir * (angle / dt) * speed_mps / 9.80665f;
            g_lat += alpha * (a_lat - g_lat);
            last_fwd = fwd;
        }
    }
};

inline GForceTracker& player_gforce_tracker() {
    static GForceTracker gf;
    return gf;
}

// ============================================================================
// Procedural Exhaust Acoustics & Engine Synthesizer
// ============================================================================
struct ProceduralAudio {
    struct PopEvent {
        float time_remaining = 0.0f;
        float duration = 0.045f;
        float intensity = 1.0f;
        float freq = 140.0f;
        float pan = 0.0f;
    };
    static constexpr int MAX_POPS = 16;
    PopEvent pops[MAX_POPS]{};

    float limiter_timer = 0.0f;
    float drs_beep_time = 0.0f;
    float prev_throttle = 0.0f;
    int prev_gear = 1;
    uint32_t rng_state = 123456789;

    float randf() {
        rng_state = rng_state * 1664525u + 1013904223u;
        return (rng_state >> 16) / 65535.0f;
    }

    void trigger_pop(float intensity, float freq = 140.0f, float delay = 0.0f) {
        for (auto& p : pops) {
            if (p.time_remaining <= 0.0f) {
                p.time_remaining = 0.045f + delay;
                p.duration = 0.045f;
                p.intensity = intensity;
                p.freq = freq + (randf() - 0.5f) * 35.0f;
                p.pan = (randf() - 0.5f) * 0.4f;
                break;
            }
        }
    }

    void trigger_drs_beep() {
        drs_beep_time = 0.14f;
    }

    void step_engine(float dt, float rpm, float throttle, int gear) {
        // 1. Overrun exhaust crackles
        if (prev_throttle > 0.55f && throttle < 0.12f && rpm > 12500.0f) {
            const int count = 3 + (int)(randf() * 4.0f);
            for (int i = 0; i < count; ++i) {
                trigger_pop(0.35f + randf() * 0.45f, 130.0f + randf() * 60.0f, i * (0.038f + randf() * 0.025f));
            }
        }
        // 2. Downshift bark
        if (gear < prev_gear && gear > 0 && rpm > 11000.0f) {
            trigger_pop(0.85f, 115.0f, 0.01f);
            trigger_pop(0.55f, 165.0f, 0.045f);
        }
        // 3. Spark-cut rev limiter stutter (RPM > 18500)
        if (rpm > 18500.0f && throttle > 0.70f) {
            limiter_timer += dt;
            if (limiter_timer >= 0.022f) {
                limiter_timer = 0.0f;
                trigger_pop(0.70f + randf() * 0.25f, 150.0f + randf() * 50.0f, 0.0f);
            }
        } else {
            limiter_timer = 0.0f;
        }

        prev_throttle = throttle;
        prev_gear = gear;
    }

    void render_mix(float* buffer, size_t frames, float sample_rate = 48000.0f) {
        const float dt = 1.0f / sample_rate;
        for (size_t i = 0; i < frames; ++i) {
            float sample_l = 0.0f, sample_r = 0.0f;

            for (auto& p : pops) {
                if (p.time_remaining > 0.0f) {
                    p.time_remaining -= dt;
                    if (p.time_remaining < p.duration) {
                        const float t = p.duration - p.time_remaining;
                        const float punch = std::sin(2.0f * 3.14159265f * p.freq * t) * std::exp(-70.0f * t);
                        const float crackle = (randf() * 2.0f - 1.0f) * std::exp(-120.0f * t);
                        const float val = std::clamp((punch * 0.75f + crackle * 0.45f) * p.intensity, -1.0f, 1.0f);
                        const float gain_l = 0.5f * (1.0f - p.pan);
                        const float gain_r = 0.5f * (1.0f + p.pan);
                        sample_l += val * gain_l * 0.40f;
                        sample_r += val * gain_r * 0.40f;
                    }
                }
            }

            if (drs_beep_time > 0.0f) {
                drs_beep_time -= dt;
                const float t = 0.14f - drs_beep_time;
                const bool pulse = (t < 0.045f) || (t > 0.07f && t < 0.115f);
                if (pulse) {
                    const float s = std::sin(2.0f * 3.14159265f * 1250.0f * t) * 0.15f;
                    sample_l += s;
                    sample_r += s;
                }
            }

            buffer[i * 2] += sample_l;
            buffer[i * 2 + 1] += sample_r;
        }
    }
};

inline ProceduralAudio& global_audio_synth() {
    static ProceduralAudio synth;
    return synth;
}

inline void render_audio_effects(float* mix_buffer, size_t frames) {
    static const bool enabled = []{
        const char* e = std::getenv("F1CE_EXHAUST_POP");
        return !e || std::strcmp(e, "0") != 0;
    }();
    if (enabled) {
        global_audio_synth().render_mix(mix_buffer, frames);
    }
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
