// Standalone checks: compile as C++20 with assertions enabled, then run.
#include "../../runtime/slick_tuning.h"

#include <cassert>
#include <cmath>

int main() {
    static_assert(slick::parse_preset("2026") == slick::Preset::modern);
    static_assert(slick::parse_preset("w11") == slick::Preset::w11);
    static_assert(slick::parse_preset("handling") == slick::parse_preset("2026"));
    static_assert(slick::parse_preset("1") == slick::Preset::basic);
    static_assert(slick::parse_preset("0") == slick::Preset::stock);
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
}
