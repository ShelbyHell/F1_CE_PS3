# Slick tyres: physics notes

[Русская версия](Physics-Notes.ru.md) · [Setup and presets](README.md)

These observations are from BCES00005 01.00 running on F1CE PC Recomp 0.4.5.

## Wear

In a manually driven dry Quick Race at Silverstone, all four `float` values starting at `car + 0x8B0` increased across six on-track samples for all 22 cars. The median across 88 wheels rose from `0.00249` to `0.02734` between 105 and 255 seconds after launch. These are internal values, not percentages.

Guest instruction `0x1149F0` adds the wear increment to the four values, and `0x114A00` clamps the result. The preset scales the increment at `0x1149D4`, before the stock clamp, without changing the fuel consumption calculation.

Two other candidates did not behave like cumulative wear: `car + 0x520` was zero in most samples, and `car + 0x8E0` started near 20 and both rose and fell while driving.

## Grip and drive torque

Guest instruction `0x114858` stores four grip multipliers at `car + 0x4D0` for the wheel physics to consume. The presets scale those values before the write. `car + 0x554` is used for diagnostics and is not modified.

Guest instruction `0x11716C` is the probe location for scaling positive drive torque. The recompiler inserts all three probes in the single-player executable only, not in `generated_online`.
