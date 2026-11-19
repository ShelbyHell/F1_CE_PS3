# Slick tyre tests

[Русская версия](README.ru.md) · [Physics notes](Physics-Notes.md)

These experimental presets target dry tyres in single-player Quick Race. Select dry weather and set **Fuel Use & Tyre Wear → Realistic**. Other tyre and race settings have not been verified.

## Setup

1. Build `build-win/f1ce.exe` with `-DF1CE_VULKAN=ON` (see [Build](../README.md#build)). The launch scripts prefer that build. If it is missing, they fall back to the corresponding local `f1ce-slick*.exe`, which may be an older snapshot without the latest options.
2. Put your `PS3_GAME` folder in the repository root, or create `Slick-Test/local.cmd` with its actual path:

   ```bat
   set "DISC=X:\Games\Formula One Championship Edition\PS3_GAME"
   ```

3. For the smooth tread textures, install Pillow (`python -m pip install Pillow`) and run this from the repository root, using your own `USRDIR` path:

   ```bat
   python Slick-Test/tools/slick_textures.py tools "X:\Games\Formula One Championship Edition\PS3_GAME\USRDIR" --build
   ```

The generator writes dry tread colour, normal, and specular maps to `MODS/SLICK_TEXTURES` under `Slick-Test`, preserving all nine mip levels. It leaves sidewalls and intermediate and wet tyres unchanged. Original and modified map previews go into `Texture-Preview`. The scripts run from `Slick-Test`, using a separate save and shader cache directory; they do not change the original game archives or launcher.

## Launch presets

In either launcher, use **Quick Race → Slick physics**, **Slick compound**, and **Slick telemetry (CSV)**. For slick visuals, separately select `SLICK_TEXTURES` as the season pack after copying its generated folder into `MODS` next to the launcher. The scripts below are an alternative and already know where to find the generated pack.

| Script | What it changes |
| --- | --- |
| `Start-Stock-Test.cmd` | Stock physics and textures, for comparison. |
| `Start-Slick-Test.cmd` | Grip multiplier ×1.05 and wear accumulation ×1.25. |
| `Start-2026-Test.cmd` | Combined **2026 (with handling)** preset, with stock textures. |
| `Start-2026-With-Textures.cmd` | The same combined preset with smooth dry tread textures. |
| `Start-Handling-Test.cmd` | Compatibility shortcut for `Start-2026-With-Textures.cmd`. |

The combined preset adds a speed-dependent grip multiplier (+14% up to 90 km/h, tapering to +8% by 260 km/h), with extra front grip at low speed and rear grip at high speed. Gamepad steering keeps full lock up to 90 km/h and tapers to at most 78% stick output from 240 km/h. Wheel, keyboard and menu input curves are unchanged. Positive drive torque can increase by up to 12%, fading to zero by 280 km/h. Rear wheelspin tapers the boost over about 45 ms; traction recovery brings it back over about 180 ms. This is a handling approximation, not a simulation of the 2026 F1 power unit. Both `F1CE_SLICK_TEST=2026` and the old `handling` value select the same preset.

### Dry compounds

Set `F1CE_SLICK_COMPOUND=soft` or `hard` before launch (or in `local.cmd`). Leave it unset for the baseline. This is a session-wide preset for eligible dry tyres, not an extra compound choice in the game's pit menu. It has no effect with stock physics.

| Compound | Wear rate vs. stock | Extra grip factor vs. the selected preset |
| --- | --- | --- |
| Baseline | ×1.25 | ×1.00 |
| Soft | ×1.55 | ×1.04 fresh, falling to ×0.92 with wear |
| Hard | ×0.90 | ×0.98 fresh, falling to ×0.965 with wear |

The grip curve uses each wheel's own wear value and saturates at `0.65` internal wear units. The factors are initial tuning targets, not measured stint lengths.

### Telemetry

Enable **Slick telemetry (CSV)** or set `F1CE_SLICK_TELEMETRY=1` before launching a script. The game writes `slick_telemetry.csv` in its working directory (`Slick-Test` for the scripts) at approximately 10 Hz while the player drives. Stock physics can log too. Each run with driving samples overwrites the previous CSV; rename it before comparing runs.

The CSV includes speed in km/h, the normalized pad steering command sent to the game (not a measured road-wheel angle), in-game throttle, gear, wheel-contact mask, and each wheel's slip flag and internal wear. Wheel indices 0–1 are front; 2–3 are rear.

## Verification

Dry Silverstone runs confirmed activation of the grip, wear, and drive changes in `f1ce_recomp.log` (look for `slick test:`). The texture run confirmed all three replacements in `offline.wad`, and the smooth tread was visible on track. Lap times, acceleration times, tyre life, and hands-on gamepad feel have not been measured.

`tools/check_tuning.cpp` is a standalone C++20 check of the steering curve, boost ramp, and compound curves. Compile it without `NDEBUG` and run it to check the tuning math without starting the game.
