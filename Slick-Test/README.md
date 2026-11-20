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

In either launcher, use **Quick Race → Slick physics**, **Slick compound**, and **Slick telemetry (CSV)**. Enable **General → Slick tread textures** for slick visuals. The toggle overlays the generated `SLICK_TEXTURES` pack without replacing the selected season pack. In a source checkout it finds the output under `Slick-Test/MODS`; a standalone installation looks in `MODS` next to the launcher.

| Script | What it changes |
| --- | --- |
| `Start-Stock-Test.cmd` | Stock physics and textures, for comparison. |
| `Start-Slick-Test.cmd` | Grip multiplier ×1.05 and wear accumulation ×1.25. |
| `Start-2026-Test.cmd` | Combined **2026 (with handling)** preset, with stock textures. |
| `Start-2026-With-Textures.cmd` | The same combined preset with smooth dry tread textures. |
| `Start-W11-With-Textures.cmd` | 2020 W11-style profile: lower low-speed mechanical grip, stronger high-speed aero-like grip, more direct gamepad steering, and smoother but smaller traction help. |
| `Start-Handling-Test.cmd` | Compatibility shortcut for `Start-2026-With-Textures.cmd`. |

The combined preset models a modest rise in effective grip with speed (+10–17.5% across the tested range), smoothly shifting a little grip balance from front at low speed toward rear at high speed. Gamepad steering keeps full lock up to 90 km/h and tapers to at most 78% stick output from 240 km/h. Wheel, keyboard and menu input curves are unchanged. Positive drive torque can increase by up to 12%, fading to zero by 280 km/h. Rear wheelspin tapers the boost over about 45 ms; traction recovery brings it back over about 180 ms. This is an experimental handling approximation, not a simulation of the 2026 F1 power unit. Both `F1CE_SLICK_TEST=2026` and the old `handling` value select the same preset.

### 2020 W11 profile

Select **2020 W11** in the launcher or set `F1CE_SLICK_TEST=w11`. The prototype applies about +8% axle grip around walking speed, increasing smoothly to +21% at 240 km/h; it shifts a little balance rearward as speed rises. Gamepad steering keeps full lock at low speed and retains 82% of full input at high speed. Traction assistance is limited to 8%, with the stock rear-wheelspin gate, so throttle control still matters. These are tuning values for this mod, not measurements or a reconstruction of Mercedes telemetry.

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

The CSV includes speed in km/h, pad steering command (not road-wheel angle), throttle, gear, contact and wheel slip, wheel load, internal wear, raw tyre flags, the current wear mode, physics eligibility, and whether the player is driving. Career dry sessions use base tyre flags `0x19` and wear mode `1`; Quick Race uses wear mode `2`. The transient `0x3000` status bits are ignored. It also samples the player car during hand-offs, such as pit-lane control; `player_driving` distinguishes those rows. Wheel indices 0–1 are front; 2–3 are rear.

To compare Stock and 2026, keep the car, track, assists, fuel load and weather the same. Mark the same corner and start/finish line crossings on each CSV's elapsed-time scale. For example, from the repository root:

```text
python Slick-Test/tools/compare_telemetry.py stock.csv modern.csv --stock-corner 45:48 --modern-corner 43:46 --stock-lap 20:110 --modern-lap 20:109
```

Repeat the lap options for a series of complete laps. The tool reports end-of-corner speed, the percentage of powered samples with rear wheelspin and both rear tyres in contact, and median lap time. Window boundaries are supplied by the driver, not inferred by the tool.

## Verification

Dry Silverstone runs confirmed activation of the grip, wear, and drive changes in `f1ce_recomp.log` (look for `slick test:`). The texture run confirmed all three replacements in `offline.wad`, and the smooth tread was visible on track. Lap times, acceleration times, tyre life, and hands-on gamepad feel have not been measured.

A dry Silverstone Quick Race after the career eligibility fix confirmed W11 grip, wear ×1.25, and progressive drive assistance. Telemetry marked the dry career mode (`0x3019`, wear mode 1) eligible, while mode 3 remained ineligible. Intermediate/wet sets have not yet been manually verified.

`tools/check_tuning.cpp` is a standalone C++20 check of the steering curve, boost ramp, and compound curves. Compile it without `NDEBUG` and run it to check the tuning math without starting the game.
