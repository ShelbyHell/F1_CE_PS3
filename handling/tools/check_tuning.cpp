// Standalone checks: compile as C++20 with assertions enabled, then run.
#undef NDEBUG
#include <cmath>
#include <iostream>
#include "../../runtime/slick_tuning.h"

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " << #cond << "\n"; \
        return 1; \
    } \
} while(0)
#define assert(cond) TEST_CHECK(cond)

int main() {
    std::cout << "Starting tests...\n" << std::flush;
    static_assert(slick::parse_preset("2026") == slick::Preset::modern);
    static_assert(slick::parse_preset("modern") == slick::Preset::modern);
    static_assert(slick::parse_preset("w11") == slick::Preset::w11);
    static_assert(slick::parse_preset("handling") == slick::parse_preset("2026"));
    static_assert(slick::parse_preset("1") == slick::Preset::basic);
    static_assert(slick::parse_preset("basic") == slick::Preset::basic);
    static_assert(slick::parse_preset("0") == slick::Preset::stock);
    static_assert(slick::parse_preset("stock") == slick::Preset::stock);
    TEST_CHECK(slick::dry_wear_state(0x19, 1));
    TEST_CHECK(slick::dry_wear_state(0x3019, 1));
    TEST_CHECK(slick::dry_wear_state(0x19, 2));
    TEST_CHECK(!slick::dry_wear_state(0x19, 3));
    TEST_CHECK(!slick::dry_wear_state(0x18, 1));
    TEST_CHECK(!slick::dry_wear_state(0x1a, 1));
    const auto close = [](float a, float b) { return std::fabs(a - b) < 0.0001f; };
    TEST_CHECK(close(slick::pad_steer(1.0f, 90.0f), 1.0f));
    TEST_CHECK(close(slick::pad_steer(-1.0f, 240.0f), -0.78f));
    TEST_CHECK(slick::pad_steer(0.3f, 240.0f) < slick::pad_steer(0.3f, 90.0f));
    TEST_CHECK(slick::pad_steer(0.0f, 200.0f) == 0.0f);

    const auto slow = slick::modern_grip(20.0f);
    const auto medium = slick::modern_grip(100.0f);
    const auto fast = slick::modern_grip(240.0f);
    TEST_CHECK(slow.front > slow.rear);
    TEST_CHECK(medium.front > medium.rear);
    TEST_CHECK(fast.rear > fast.front);
    TEST_CHECK(fast.front > slow.front && fast.rear > slow.rear);
    TEST_CHECK(close(slick::modern_grip(400.0f).rear, fast.rear));
    const auto w11_slow = slick::w11_grip(40.0f), w11_fast = slick::w11_grip(260.0f);
    TEST_CHECK(w11_fast.front > w11_slow.front && w11_fast.rear > w11_slow.rear);
    TEST_CHECK(slick::w11_pad_steer(0.5f, 250.0f) > slick::pad_steer(0.5f, 250.0f));
    TEST_CHECK(slick::drive_boost_ceiling(slick::Preset::w11, 100.0f, 1.0f) > 0.0f);

    slick::DriveResponse drive;
    TEST_CHECK(drive.step(0.12f, false) > 0.0f && drive.boost < 0.12f);
    for (int i = 0; i < 32; ++i) drive.step(0.12f, false);
    TEST_CHECK(close(drive.boost, 0.12f));
    TEST_CHECK(drive.step(0.12f, true) > 0.0f && drive.boost < 0.12f);
    for (int i = 0; i < 8; ++i) drive.step(0.12f, true);
    TEST_CHECK(close(drive.boost, 0.0f));
    for (int i = 0; i < 33; ++i) drive.step(0.12f, false);
    TEST_CHECK(close(drive.step(0.01f, false), 0.01f));

    using slick::Compound;
    TEST_CHECK(slick::wear_multiplier(Compound::soft) > slick::wear_multiplier(Compound::baseline));
    TEST_CHECK(slick::wear_multiplier(Compound::hard) < 1.0f);
    TEST_CHECK(slick::compound_grip(Compound::soft, 0.0f) > slick::compound_grip(Compound::hard, 0.0f));
    TEST_CHECK(slick::compound_grip(Compound::soft, 0.65f) < slick::compound_grip(Compound::hard, 0.65f));
    TEST_CHECK(close(slick::compound_grip(Compound::soft, 1.0f), slick::compound_grip(Compound::soft, 0.65f)));

    // Wake / Dirty Air / Slipstream checks
    const slick::Vec3 p_pos{0.0f, 0.0f, 0.0f};
    const slick::Vec3 p_fwd{0.0f, 0.0f, 1.0f};
    const slick::Vec3 rivals[3] = {
        {0.0f, 0.0f, 15.0f},   // 15m ahead, directly in line
        {10.0f, 0.0f, 20.0f},  // 20m ahead, far to right (outside cone)
        {0.0f, 0.0f, -10.0f}   // 10m behind
    };
    const auto wake_close = slick::evaluate_wake(p_pos, p_fwd, rivals, 3, 220.0f, 0.05f);
    TEST_CHECK(wake_close.dist_ahead > 14.9f && wake_close.dist_ahead < 15.1f);
    TEST_CHECK(wake_close.intensity > 0.6f);
    TEST_CHECK(wake_close.in_slipstream);

    const auto aero_penalty = slick::dirty_air_penalty(wake_close, 220.0f);
    TEST_CHECK(aero_penalty.front > aero_penalty.rear);
    TEST_CHECK(aero_penalty.front > 0.03f);

    const auto draft = slick::slipstream_boost(wake_close, 240.0f, 1.0f);
    TEST_CHECK(draft > 0.03f && draft <= 0.06f);

    // Clean air check
    const slick::Vec3 no_rivals[1] = {{0.0f, 0.0f, -30.0f}};
    const auto clean = slick::evaluate_wake(p_pos, p_fwd, no_rivals, 1, 240.0f, 0.0f);
    TEST_CHECK(clean.intensity == 0.0f);
    TEST_CHECK(!clean.in_slipstream);
    TEST_CHECK(slick::dirty_air_penalty(clean, 240.0f).front == 0.0f);
    TEST_CHECK(slick::slipstream_boost(clean, 240.0f, 1.0f) == 0.0f);

    // Trail braking checks
    // Straight braking: high front load, 0 steer -> no asymmetry
    const auto straight_brake = slick::trail_brake_grip(180.0f, 0.0f, 4000.0f, 4000.0f, 1500.0f, 1500.0f, 0.56f);
    TEST_CHECK(straight_brake.fl_mult == 1.0f && straight_brake.fr_mult == 1.0f);
    TEST_CHECK(straight_brake.rl_mult == 1.0f && straight_brake.rr_mult == 1.0f);

    // Trail braking into left turn (steer < 0): inside is FL (load_0), outside is FR (load_1)
    // Moderate trail: loaded outside gets bite (+), rear rotates (-)
    const auto trail_left = slick::trail_brake_grip(120.0f, -0.30f, 2000.0f, 5000.0f, 1200.0f, 1800.0f, 0.56f);
    TEST_CHECK(trail_left.fr_mult > 1.0f); // outside front bite
    TEST_CHECK(trail_left.rl_mult < 1.0f); // inside rear rotation

    // Over-braking into left turn with heavily unloaded inside FL (< 20% of front axle load):
    const auto hard_scrub_left = slick::trail_brake_grip(100.0f, -0.60f, 600.0f, 6000.0f, 800.0f, 1800.0f, 0.58f);
    TEST_CHECK(hard_scrub_left.fl_mult < 1.0f); // inside front scrubs

    // ========================================================================
    // Tyre Thermodynamics & Operating Thermal Windows
    // ========================================================================
    const auto win_soft = slick::compound_thermal_window(Compound::soft);
    const auto win_hard = slick::compound_thermal_window(Compound::hard);
    const auto win_base = slick::compound_thermal_window(Compound::baseline);

    TEST_CHECK(win_soft.opt_min == 85.0f && win_soft.opt_max == 105.0f);
    TEST_CHECK(win_hard.opt_min == 95.0f && win_hard.opt_max == 115.0f);
    TEST_CHECK(win_base.opt_min == 80.0f && win_base.opt_max == 102.0f);

    // Thermal grip tests
    // 1. Optimum thermal window yields 100% grip (1.0f)
    TEST_CHECK(slick::thermal_grip_factor(Compound::soft, 85.0f) == 1.0f);
    TEST_CHECK(slick::thermal_grip_factor(Compound::soft, 95.0f) == 1.0f);
    TEST_CHECK(slick::thermal_grip_factor(Compound::soft, 105.0f) == 1.0f);

    TEST_CHECK(slick::thermal_grip_factor(Compound::hard, 95.0f) == 1.0f);
    TEST_CHECK(slick::thermal_grip_factor(Compound::hard, 105.0f) == 1.0f);
    TEST_CHECK(slick::thermal_grip_factor(Compound::hard, 115.0f) == 1.0f);

    // 2. Cold tyres have reduced grip
    const float cold_grip_30 = slick::thermal_grip_factor(Compound::soft, 30.0f);
    const float cold_grip_60 = slick::thermal_grip_factor(Compound::soft, 60.0f);
    const float warm_grip_80 = slick::thermal_grip_factor(Compound::soft, 80.0f);
    TEST_CHECK(cold_grip_30 < cold_grip_60);
    TEST_CHECK(cold_grip_60 < warm_grip_80);
    TEST_CHECK(warm_grip_80 < 1.0f);
    TEST_CHECK(cold_grip_30 >= 0.80f); // cold slick drop-off

    // 3. Overheated tyres have reduced grip (greasy / blistered)
    const float hot_grip_110 = slick::thermal_grip_factor(Compound::soft, 110.0f);
    const float hot_grip_125 = slick::thermal_grip_factor(Compound::soft, 125.0f);
    const float hot_grip_140 = slick::thermal_grip_factor(Compound::soft, 140.0f);
    TEST_CHECK(hot_grip_110 < 1.0f);
    TEST_CHECK(hot_grip_125 < hot_grip_110);
    TEST_CHECK(hot_grip_140 < hot_grip_125);
    TEST_CHECK(hot_grip_140 >= 0.70f);

    // Thermal wear factor tests
    TEST_CHECK(slick::thermal_wear_factor(Compound::soft, 95.0f) == 1.0f);
    TEST_CHECK(slick::thermal_wear_factor(Compound::soft, 105.0f) == 1.0f);
    const float wear_hot_115 = slick::thermal_wear_factor(Compound::soft, 115.0f);
    const float wear_hot_130 = slick::thermal_wear_factor(Compound::soft, 130.0f);
    TEST_CHECK(wear_hot_115 > 1.0f);
    TEST_CHECK(wear_hot_130 > wear_hot_115);
    TEST_CHECK(wear_hot_130 <= 3.5f);

    // Thermochromic color tests
    const auto col_cold = slick::thermochromic_color(50.0f, Compound::soft);
    TEST_CHECK(col_cold.b > col_cold.r && col_cold.b > col_cold.g); // Deep Blue dominant

    const auto col_opt = slick::thermochromic_color(95.0f, Compound::soft);
    TEST_CHECK(col_opt.g == 195 && col_opt.r == 0); // Pure Siemens Green

    const auto col_warm = slick::thermochromic_color(116.0f, Compound::soft);
    TEST_CHECK(col_warm.r > 200 && col_warm.g > 100); // Amber

    const auto col_overheat = slick::thermochromic_color(130.0f, Compound::soft);
    TEST_CHECK(col_overheat.r > col_overheat.g && col_overheat.r > col_overheat.b); // Red dominant

    // Dynamic wheel thermal integration
    slick::WheelThermal wt;
    wt.surface = 85.0f;
    wt.carcass = 85.0f;

    // Cornering scrub under load at 220 km/h (61 m/s) with steer = 0.35f
    const float dt = 0.00555f; // ~180 Hz substep
    for (int i = 0; i < 360; ++i) { // 2 seconds of hard cornering
        wt.step(dt, 61.0f, 4500.0f, false, 0.35f, 0.8f, true, Compound::soft);
    }
    TEST_CHECK(wt.surface > 85.0f); // Surface heats up from scrub
    TEST_CHECK(wt.carcass > 85.0f); // Carcass absorbs heat

    // Severe lockup under heavy braking (1.0 second slide)
    const float pre_lock = wt.surface;
    for (int i = 0; i < 180; ++i) {
        wt.step(dt, 40.0f, 5000.0f, true, 0.0f, 0.0f, true, Compound::soft);
    }
    TEST_CHECK(wt.surface > pre_lock + 5.0f); // Noticeable spike from lockup friction

    // Straight line cooling at high speed (300 km/h [83 m/s], 0 steer, 0 slip)
    const float pre_cool = wt.surface;
    for (int i = 0; i < 900; ++i) { // 5 seconds on straight
        wt.step(dt, 83.0f, 3000.0f, false, 0.0f, 1.0f, true, Compound::soft);
    }
    TEST_CHECK(wt.surface < pre_cool); // High speed airflow cools the surface

    // ========================================================================
    // Carbon-Carbon Brake Thermodynamics & Fade Checks
    // ========================================================================
    slick::BrakeDiscThermal brake;
    TEST_CHECK(brake.temp == 450.0f);

    // Heavy braking from 300 km/h (~83 m/s) with 100% demand
    for (int i = 0; i < 360; ++i) { // 2 seconds threshold braking
        brake.step(dt, 83.0f, 1.0f, 0.58f, 4200.0f);
    }
    TEST_CHECK(brake.temp > 700.0f); // Substantial thermal spike into glowing range

    // High speed cooling on long straight with 0% braking
    const float brake_hot = brake.temp;
    for (int i = 0; i < 900; ++i) { // 5 seconds cooling down straight
        brake.step(dt, 83.0f, 0.0f, 0.58f, 2500.0f);
    }
    TEST_CHECK(brake.temp < brake_hot); // Forced air duct cooling dissipates heat

    // Brake fade factor tests
    TEST_CHECK(slick::brake_fade_factor(150.0f) < 1.0f);  // Cold bite reduction
    TEST_CHECK(slick::brake_fade_factor(150.0f) >= 0.85f);
    TEST_CHECK(slick::brake_fade_factor(550.0f) == 1.0f);  // Optimal operating window
    TEST_CHECK(slick::brake_fade_factor(800.0f) == 1.0f);  // Still in window
    TEST_CHECK(slick::brake_fade_factor(950.0f) < 1.0f);   // Progressive fade
    TEST_CHECK(slick::brake_fade_factor(950.0f) > 0.85f);
    TEST_CHECK(slick::brake_fade_factor(1150.0f) <= 0.85f); // Severe overheat fade
    TEST_CHECK(slick::brake_fade_factor(1200.0f) >= 0.70f);

    // Incandescent brake disc glow colors
    const auto col_b_cold = slick::incandescent_brake_color(250.0f);
    TEST_CHECK(col_b_cold.r == 80 && col_b_cold.g == 95 && col_b_cold.b == 110); // Cold steel slate

    const auto col_b_dull = slick::incandescent_brake_color(550.0f);
    TEST_CHECK(col_b_dull.r > 120 && col_b_dull.g < 80); // Dull cherry red

    const auto col_b_orange = slick::incandescent_brake_color(750.0f);
    TEST_CHECK(col_b_orange.r > 200 && col_b_orange.g > 80); // Glowing orange

    const auto col_b_incand = slick::incandescent_brake_color(980.0f);
    TEST_CHECK(col_b_incand.r == 255 && col_b_incand.g > 150); // Incandescent yellow

    const auto col_b_white = slick::incandescent_brake_color(1180.0f);
    TEST_CHECK(col_b_white.r == 255 && col_b_white.g > 220 && col_b_white.b > 180); // White-hot glow

    // ========================================================================
    // Drag Reduction System (DRS) Controller Checks
    // ========================================================================
    slick::DrsController drs;
    TEST_CHECK(drs.state == slick::DrsState::Closed);

    slick::WakeInfo trailing_wake{};
    trailing_wake.in_slipstream = true;
    trailing_wake.dist_ahead = 25.0f;
    trailing_wake.intensity = 0.5f;

    // 1. In a corner, DRS is firmly Closed
    for (int i = 0; i < 40; ++i) drs.update(0.016f, 130.0f, 0.5f, 0.35f, 0.0f, trailing_wake, false, false, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Closed);

    // 2. Corner exit onto straight: Detection Point triggers and Arms DRS when trailing rival
    drs.update(0.016f, 150.0f, 0.85f, 0.02f, 0.0f, trailing_wake, false, false, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Armed);

    // 3. Accelerating down straight across activation line: DRS becomes Available
    for (int i = 0; i < 50; ++i) drs.update(0.016f, 210.0f, 1.0f, 0.0f, 0.0f, trailing_wake, false, false, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Available);

    // 4. Driver presses DRS button -> DRS Opens
    drs.update(0.016f, 235.0f, 1.0f, 0.0f, 0.0f, trailing_wake, true, false, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Open);

    // 5. Stays open while holding throttle on straight
    drs.update(0.016f, 260.0f, 1.0f, 0.0f, 0.0f, trailing_wake, false, false, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Open);

    // 6. Driver taps brake -> Instant cancellation to Closed & Disarmed
    drs.update(0.016f, 245.0f, 0.0f, 0.0f, 0.35f, trailing_wake, false, false, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Closed);

    // 7. Mid-corner acceleration -> Stays Closed (cannot re-open mid-corner)
    drs.update(0.016f, 140.0f, 1.0f, 0.45f, 0.0f, trailing_wake, true, true, true, false);
    TEST_CHECK(drs.state == slick::DrsState::Closed);

    // 8. Time Trial Mode: Detection point arms without requiring a rival car ahead
    slick::DrsController tt_drs;
    slick::WakeInfo empty_wake{}; // No rival cars

    // 8a. Standstill / pit exit -> Closed
    tt_drs.update(0.016f, 30.0f, 0.5f, 0.0f, 0.0f, empty_wake, false, false, true, true);
    TEST_CHECK(tt_drs.state == slick::DrsState::Closed);

    // 8b. Turning through corner before straight
    for (int i = 0; i < 40; ++i) tt_drs.update(0.016f, 120.0f, 0.5f, 0.35f, 0.0f, empty_wake, false, false, true, true);
    TEST_CHECK(tt_drs.state == slick::DrsState::Closed);

    // 8c. Unwinding wheel at corner exit onto straight -> Detection Point Arms DRS!
    tt_drs.update(0.016f, 145.0f, 0.90f, 0.02f, 0.0f, empty_wake, false, false, true, true);
    TEST_CHECK(tt_drs.state == slick::DrsState::Armed);

    // 8d. Accelerating along straight across activation line with auto_drs -> Opens automatically
    for (int i = 0; i < 50; ++i) tt_drs.update(0.016f, 220.0f, 1.0f, 0.0f, 0.0f, empty_wake, false, true, true, true);
    TEST_CHECK(tt_drs.state == slick::DrsState::Open);

    // 8e. Heavy braking into corner -> Closes & Disarms
    tt_drs.update(0.016f, 250.0f, 0.0f, 0.0f, 0.80f, empty_wake, false, false, true, true);
    TEST_CHECK(tt_drs.state == slick::DrsState::Closed);

    // 8f. Mid-corner throttle -> Stays Closed
    tt_drs.update(0.016f, 110.0f, 0.85f, 0.35f, 0.0f, empty_wake, false, true, true, true);
    TEST_CHECK(tt_drs.state == slick::DrsState::Closed);

    // ========================================================================
    // G-Force Vector Tracker Checks
    // ========================================================================
    slick::GForceTracker gf;
    gf.last_speed_mps = 80.0f;
    gf.last_fwd = {0.0f, 0.0f, 1.0f};

    // Heavy deceleration from 80 m/s down to 60 m/s
    gf.update(0.05f, 60.0f, {0.0f, 0.0f, 1.0f});
    TEST_CHECK(gf.g_long < -1.0f); // Longitudinal braking G

    // Cornering test: direction changes laterally
    gf.update(0.05f, 60.0f, {0.15f, 0.0f, 0.988f});
    TEST_CHECK(std::fabs(gf.g_lat) > 0.5f); // Lateral cornering G

    // ========================================================================
    // Procedural Audio Synthesizer Checks
    // ========================================================================
    slick::ProceduralAudio synth;

    // 1. Overrun pops trigger when lifting throttle at high RPM
    synth.prev_throttle = 0.90f;
    synth.step_engine(0.016f, 16000.0f, 0.05f, 5);
    bool has_pop = false;
    for (const auto& p : synth.pops) {
        if (p.time_remaining > 0.0f) has_pop = true;
    }
    TEST_CHECK(has_pop);

    // 2. Downshift bark trigger
    synth.prev_gear = 5;
    synth.step_engine(0.016f, 14000.0f, 0.20f, 4);
    TEST_CHECK(synth.pops[0].time_remaining > 0.0f);

    // 3. DRS chime trigger
    synth.trigger_drs_beep();
    TEST_CHECK(synth.drs_beep_time > 0.10f);

    // 4. Render stereo mix buffer
    float audio_buffer[512] = {}; // 256 stereo frames
    synth.render_mix(audio_buffer, 256, 48000.0f);
    float energy = 0.0f;
    for (int i = 0; i < 512; ++i) {
        energy += std::fabs(audio_buffer[i]);
    }
    TEST_CHECK(energy > 0.01f); // Mix contains synthesized sound waves

    std::cout << "All tuning checks passed!\n";
    return 0;
}
