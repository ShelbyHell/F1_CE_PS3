# Slick tyre tests

[Русская версия](README.ru.md) · [Physics notes](Physics-Notes.md)

These experimental presets target dry tyres in single-player Quick Race. Select dry weather and set **Fuel Use & Tyre Wear → Realistic**. Other tyre and race settings have not been verified.

## Setup

1. Build `build-win/f1ce.exe` with `-DF1CE_VULKAN=ON` (see [Build](../README.md#build)). The launch scripts use that build unless the corresponding local executable is present in `Slick-Test`: `f1ce-slick.exe`, `f1ce-slick-2026.exe`, or `f1ce-slick-handling.exe`.
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

| Script | What it changes |
| --- | --- |
| `Start-Stock-Test.cmd` | Stock physics and textures, for comparison. |
| `Start-Slick-Test.cmd` | Grip multiplier ×1.05 and wear accumulation ×1.25. |
| `Start-2026-Test.cmd` | Grip multiplier rises by 14% up to 90 km/h, tapering to 8% at 260 km/h. Adds up to 12% to positive drive torque under throttle while in gear and without rear wheelspin; the boost fades to zero at 280 km/h. Wear accumulation stays at ×1.25. |
| `Start-2026-With-Textures.cmd` | The same 2026 preset with smooth dry tread textures. |
| `Start-Handling-Test.cmd` | Builds on the textured 2026 preset: gentler gamepad steering as speed rises, full lock up to 90 km/h, and at most 78% stick output from 240 km/h. Adds front grip at low speed and rear grip at high speed. Wheel, keyboard and menu inputs are unchanged. |

The 2026 preset is a handling approximation, not a simulation of the 2026 F1 power unit. Compare it against the stock preset, or compare the two textured presets to isolate the handling changes.

## Verification

Dry Silverstone runs confirmed activation of the grip, wear, and drive changes in `f1ce_recomp.log` (look for `slick test:`). The texture run confirmed all three replacements in `offline.wad`, and the smooth tread was visible on track. Lap times, acceleration times, tyre life, and hands-on gamepad feel have not been measured.
