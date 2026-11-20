// Standalone checks: compile as C++20 with assertions enabled, then run.
#include "../../runtime/slick_tuning.h"

#include <cassert>
#include <cmath>

int main() {
    static_assert(slick::parse_preset("2026") == slick::Preset::modern);
    static_assert(slick::parse_preset("modern") == slick::Preset::modern);
    static_assert(slick::parse_preset("w11") == slick::Preset::w11);
    static_assert(slick::parse_preset("handling") == slick::parse_preset("2026"));
    static_assert(slick::parse_preset("1") == slick::Preset::basic);
    static_assert(slick::parse_preset("basic") == slick::Preset::basic);
    static_assert(slick::parse_preset("0") == slick::Preset::stock);
    static_assert(slick::parse_preset("stock") == slick::Preset::stock);
    assert(slick::dry_wear_state(0x19, 1));
    assert(slick::dry_wear_state(0x3019, 1));
    assert(slick::dry_wear_state(0x19, 2));
    assert(!slick::dry_wear_state(0x19, 3));
    assert(!slick::dry_wear_state(0x18, 1));
    assert(!slick::dry_wear_state(0x1a, 1));
    const auto close = [](float a, float b) { return std::fabs(a - b) < 0.0001f; };
    assert(close(slick::pad_steer(1.0f, 90.0f), 1.0f));
    assert(close(slick::pad_steer(-1.0f, 240.0f), -0.78f));
    assert(slick::pad_steer(0.3f, 240.0f) < slick::pad_steer(0.3f, 90.0f));
    assert(slick::pad_steer(0.0f, 200.0f) == 0.0f);

    const auto slow = slick::modern_grip(20.0f);
    const auto medium = slick::modern_grip(100.0f);
    const auto fast = slick::modern_grip(240.0f);
    assert(slow.front > slow.rear);
    assert(medium.front > medium.rear);
    assert(fast.rear > fast.front);
    assert(fast.front > slow.front && fast.rear > slow.rear);
    assert(close(slick::modern_grip(400.0f).rear, fast.rear));
    const auto w11_slow = slick::w11_grip(40.0f), w11_fast = slick::w11_grip(260.0f);
    assert(w11_fast.front > w11_slow.front && w11_fast.rear > w11_slow.rear);
    assert(slick::w11_pad_steer(0.5f, 250.0f) > slick::pad_steer(0.5f, 250.0f));
    assert(slick::drive_boost_ceiling(slick::Preset::w11, 100.0f, 1.0f) > 0.0f);

    slick::DriveResponse drive;
    assert(drive.step(0.12f, false) > 0.0f && drive.boost < 0.12f);
    for (int i = 0; i < 32; ++i) drive.step(0.12f, false);
    assert(close(drive.boost, 0.12f));
    assert(drive.step(0.12f, true) > 0.0f && drive.boost < 0.12f);
    for (int i = 0; i < 8; ++i) drive.step(0.12f, true);
    assert(close(drive.boost, 0.0f));
    for (int i = 0; i < 33; ++i) drive.step(0.12f, false);
    assert(close(drive.step(0.01f, false), 0.01f));

    using slick::Compound;
    assert(slick::wear_multiplier(Compound::soft) > slick::wear_multiplier(Compound::baseline));
    assert(slick::wear_multiplier(Compound::hard) < 1.0f);
    assert(slick::compound_grip(Compound::soft, 0.0f) > slick::compound_grip(Compound::hard, 0.0f));
    assert(slick::compound_grip(Compound::soft, 0.65f) < slick::compound_grip(Compound::hard, 0.65f));
    assert(close(slick::compound_grip(Compound::soft, 1.0f), slick::compound_grip(Compound::soft, 0.65f)));

    // Wake / Dirty Air / Slipstream checks
    const slick::Vec3 p_pos{0.0f, 0.0f, 0.0f};
    const slick::Vec3 p_fwd{0.0f, 0.0f, 1.0f};
    const slick::Vec3 rivals[3] = {
        {0.0f, 0.0f, 15.0f},   // 15m ahead, directly in line
        {10.0f, 0.0f, 20.0f},  // 20m ahead, far to right (outside cone)
        {0.0f, 0.0f, -10.0f}   // 10m behind
    };
    const auto wake_close = slick::evaluate_wake(p_pos, p_fwd, rivals, 3, 220.0f, 0.05f);
    assert(wake_close.dist_ahead > 14.9f && wake_close.dist_ahead < 15.1f);
    assert(wake_close.intensity > 0.6f);
    assert(wake_close.in_slipstream);

    const auto aero_penalty = slick::dirty_air_penalty(wake_close, 220.0f);
    assert(aero_penalty.front > aero_penalty.rear);
    assert(aero_penalty.front > 0.03f);

    const auto draft = slick::slipstream_boost(wake_close, 240.0f, 1.0f);
    assert(draft > 0.03f && draft <= 0.06f);

    // Clean air check
    const slick::Vec3 no_rivals[1] = {{0.0f, 0.0f, -30.0f}};
    const auto clean = slick::evaluate_wake(p_pos, p_fwd, no_rivals, 1, 240.0f, 0.0f);
    assert(clean.intensity == 0.0f);
    assert(!clean.in_slipstream);
    assert(slick::dirty_air_penalty(clean, 240.0f).front == 0.0f);
    assert(slick::slipstream_boost(clean, 240.0f, 1.0f) == 0.0f);

    // Trail braking checks
    // Straight braking: high front load, 0 steer -> no asymmetry
    const auto straight_brake = slick::trail_brake_grip(180.0f, 0.0f, 4000.0f, 4000.0f, 1500.0f, 1500.0f, 0.56f);
    assert(straight_brake.fl_mult == 1.0f && straight_brake.fr_mult == 1.0f);
    assert(straight_brake.rl_mult == 1.0f && straight_brake.rr_mult == 1.0f);

    // Trail braking into left turn (steer < 0): inside is FL (load_0), outside is FR (load_1)
    // Moderate trail: loaded outside gets bite (+), rear rotates (-)
    const auto trail_left = slick::trail_brake_grip(120.0f, -0.30f, 2000.0f, 5000.0f, 1200.0f, 1800.0f, 0.56f);
    assert(trail_left.fr_mult > 1.0f); // outside front bite
    assert(trail_left.rl_mult < 1.0f); // inside rear rotation

    // Over-braking into left turn with heavily unloaded inside FL (< 20% of front axle load):
    const auto hard_scrub_left = slick::trail_brake_grip(100.0f, -0.60f, 600.0f, 6000.0f, 800.0f, 1800.0f, 0.58f);
    assert(hard_scrub_left.fl_mult < 1.0f); // inside front scrubs
}
