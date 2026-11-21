# Vehicle Handling and Physics Subsystem

[Русская версия](README.ru.md) · [Physics notes](Physics-Notes.md)

This subsystem models advanced dry vehicle handling, tyre compound characteristics, aerodynamic slipstream and dirty air, dynamic trail braking, and refined gamepad steering response for single-player races. Select dry weather and set **Fuel Use & Tyre Wear → Realistic**. Other tyre and race settings have not been verified.

## Setup

1. Build `build-win/f1ce.exe` with `-DF1CE_VULKAN=ON` (see [Build](../README.md#build)). The launch scripts prefer that build. If it is missing, they fall back to the corresponding local `f1ce-slick*.exe`, which may be an older snapshot without the latest options.
2. Put your `PS3_GAME` folder in the repository root, or create `handling/local.cmd` with its actual path:

   ```bat
   set "DISC=X:\Games\Formula One Championship Edition\PS3_GAME"
   ```

3. For the smooth slick tread textures, install Pillow (`python -m pip install Pillow`) and run this from the repository root, using your own `USRDIR` path:

   ```bat
   python handling/tools/slick_textures.py tools "X:\Games\Formula One Championship Edition\PS3_GAME\USRDIR" --build
   ```

The generator writes dry tread colour, normal, and specular maps to `MODS/SLICK_TEXTURES` under `handling`, preserving all nine mip levels. It leaves sidewalls and intermediate and wet tyres unchanged. Original and modified map previews go into `Texture-Preview`. The scripts run from `handling`, using a separate save and shader cache directory; they do not change the original game archives or launcher.

## Launch presets

In either launcher, use **Quick Race → Handling model**, **Tyre compound**, and **Physics telemetry (CSV)**. Enable **General → Slick tread textures** for slick visuals. The toggle overlays the generated `SLICK_TEXTURES` pack without replacing the selected season pack. In a source checkout it finds the output under `handling/MODS`; a standalone installation looks in `MODS` next to the launcher.

| Script | What it changes |
| --- | --- |
| `Start-Handling.cmd` | Unified launcher script: accepts preset (`2026`, `w11`, `basic`, `stock`) and optional `textures`. |
| `Start-Stock-Test.cmd` | Stock physics and textures, for comparison. |
| `Start-Slick-Test.cmd` | Compatibility forwarder to `Start-Handling.cmd`. |
| `Start-2026-Test.cmd` | Combined **Modern 2026 handling** preset, with stock textures. |
| `Start-2026-With-Textures.cmd` | Modern 2026 handling with smooth dry tread textures. |
| `Start-W11-With-Textures.cmd` | 2020 W11-style profile with smooth dry tread textures. |
| `Start-Handling-Test.cmd` | Compatibility shortcut for `Start-2026-With-Textures.cmd`. |

The combined Modern preset models a modest rise in effective grip with speed (+10–17.5% across the tested range), smoothly shifting a little grip balance from front at low speed toward rear at high speed. Gamepad steering keeps full lock up to 90 km/h and tapers to at most 78% stick output from 240 km/h. Wheel, keyboard and menu input curves are unchanged. Positive drive torque can increase by up to 12%, fading to zero by 280 km/h. Rear wheelspin tapers the boost over about 45 ms; traction recovery brings it back over about 180 ms. This is an experimental handling approximation, not a simulation of the 2026 F1 power unit. Both `F1CE_HANDLING=2026` and legacy `F1CE_SLICK_TEST=2026` / `handling` select this preset.

### 2020 W11 profile

Select **2020 W11 handling** in the launcher or set `F1CE_HANDLING=w11` (or `F1CE_SLICK_TEST=w11`). The profile applies about +8% axle grip around walking speed, increasing smoothly to +21% at 240 km/h; it shifts a little balance rearward as speed rises. Gamepad steering keeps full lock at low speed and retains 82% of full input at high speed. Traction assistance is limited to 8%, with the stock rear-wheelspin gate, so throttle control still matters.

### Aerodynamics: Dirty Air & Slipstream

When running modern or W11 handling presets, aerodynamic interaction between cars is actively modeled (can be disabled via launcher or `F1CE_DIRTY_AIR=0`):
- **Dirty Air (Turbulent Wake):** When trailing another car closely (within ~65 m) in medium/high-speed corners (>80 km/h), the front wing washes out, losing up to 6.5% front grip and 2.5% rear grip, reproducing authentic understeer in traffic.
- **Slipstream (Drafting Tow):** When running behind a car on high-speed straights (>160 km/h, straight steering, full throttle), aerodynamic drag reduction grants up to +6% extra drive assist to help pull off overtakes into braking zones.
- The telemetry CSV records `wake_dist` and `wake_intensity` (0.000 to 1.000) for analysis.

### Braking & Trail Braking

Corner entry under braking dynamically evaluates load transfer across axles:
- **Trail Braking:** Easing off the brakes while turning loads the outside front tyre (+2.5% grip reward) while allowing the rear axle to rotate smoothly toward the apex.
- **Inside Wheel Lockup / Scrub:** Over-braking into a turn with an unloaded inside front tyre causes progressive grip scrub and understeer.
- **Brake Bias:** Configurable via launcher or `F1CE_BRAKE_BIAS` (default `56` for 56% front; `58` for high-speed stability; `54` for agile low-speed rotation).

### Dynamic Tyre Thermodynamics & Thermal Windows

The tyre model features a dual-layer thermodynamic simulation (tread surface and structural carcass):
- **Heat Generation:**
  - *Lateral Scrub Friction:* Cornering slip work ($F_y \cdot v_{slip}$) heats the tyre surface proportional to steering angle, speed, and lateral load.
  - *Longitudinal Friction:* Wheelspin under power and lockup slides under braking rapidly spike surface temperatures.
  - *Carcass Flexure:* Cyclic rolling deformation under aerodynamic and chassis downforce ($F_z$) heats the inner carcass.
- **Convective Airflow Cooling & Conduction:** High-speed airflow convective cooling ($h(v) \cdot (T - T_{amb})$) dissipates heat on straights, while thermal conduction transfers heat between surface and carcass.
- **Compound Operating Windows:**
  - **Soft:** 85–105 °C (peak 95 °C). High initial grip (+4%), rapid warm-up, but prone to thermal degradation if overdriven.
  - **Hard:** 95–115 °C (peak 105 °C). Requires vigorous warm-up laps; highly resistant to overheating and long-stint degradation.
  - **Baseline:** 80–102 °C (peak 90 °C). Balanced general window.
- **Thermal Grip & Wear Scaling:**
  - Operating within the compound window delivers 100% grip.
  - Cold tyres experience progressive grip drop-off (down to ~84% at ambient).
  - Overheated tyres become blistered/greasy, losing up to 30% grip and multiplying tyre degradation up to 3.5× normal wear rates.

### In-Game Tyre HUD Widget

Enable **Tyre HUD widget** in the launcher or set `F1CE_TYRE_HUD=1`. A compact on-screen dashboard styled in authentic Formula 1 2006 FOM / Siemens TV broadcast graphics displays directly above the in-game speedometer/rev-counter in the lower right:
- Authentic Siemens petrol/teal header (`SIEMENS`) with slanted FOM 2006 styling and compound badge (`SOFT`, `HARD`, `TYRES`).
- F1 chassis silhouette with four individual tyre pods (FL, FR, RL, RR) displaying live temperature in °C, wear life %, and grooved/slick pattern.
- **Thermochromic Broadcast Palette:** Smoothly transitions tyre pod colors reflecting thermodynamic status:
  - Deep cold blue / cyan (<80 °C) $\to$ authentic Siemens green (85–105 °C optimal) $\to$ amber (105–115 °C hot) $\to$ warning red (>120 °C severe overheat).
- **Carbon-Carbon Brake Disc Temperatures & Glow:** Displays live disc temperatures in °C with incandescent thermal radiation coloring:
  - Cold slate (<400 °C) $\to$ dull red (400–600 °C) $\to$ glowing orange (600–850 °C) $\to$ bright incandescent yellow (850–1050 °C) $\to$ white-hot (>1050 °C), complete with vertical disc thermal glow bars.
- **Pedal Input Traces:** Live vertical Throttle (green) and Brake (red) input pressure meters on the left column.
- **Circular FOM G-Force Dial:** Concentric G-force meter with 2D lateral/longitudinal acceleration vector pip and live total G readout.
- **DRS Status Badge:** Real-time indicator in the header bar showing `DRS` (closed), `DRS AVAIL` (yellow, eligible on straight), or `DRS OPEN` (cyan, active).
- Integrated wear life percentage and miniature progress bar at the base of each tyre pod.
- White and electric cyan pulsing borders when wheelspin or lockup slip occurs.
- Telemetry footer with aerodynamic wake status (`CLEAN AIR`, `TURBULENCE`, `SLIPSTREAM`) and brake bias setting (`BRAKE BIAS 56%`).
- Positioned seamlessly above the game's native rev counter without overlapping or gamepad/wheel input interference.

### Carbon Brake Disc Thermodynamics & Fade

Formula 1 Carbon-Carbon brake discs operate in a demanding thermal envelope between 300 °C and 1200 °C:
- **Thermal Generation & Dissipation:** Disc heat is generated dynamically from speed, brake demand, axle load, and brake bias. Discs cool via forced duct convection proportional to vehicle speed and high-temperature Stefan-Boltzmann radiation.
- **Thermal Fade Curve:**
  - Cold bite reduction (<350 °C): discs require warming up for peak bite.
  - Optimal friction window (350–850 °C): 100% full braking efficiency.
  - Thermal fade (850–1050 °C): progressive bite loss under extended heavy braking.
  - Severe overheat fade (>1050 °C): up to 30% reduction in braking grip, demanding careful brake management.
- **Carcass Heat Soaking:** Sustained high brake disc temperatures (>500 °C) radiate heat through the hub into the tyre carcass, elevating carcass pressures and temperatures.

### Drag Reduction System (DRS)

Modern active aerodynamics rear wing flap control:
- **Detection & Eligibility:** Activates on straights at speeds >175 km/h, throttle >80%, and steering angle <0.16 rad when running within 1.0s / slipstream cone of a preceding car (or anytime if `F1CE_DRS=always`).
- **Activation Modes:**
  - Manual (default): Press the controller Triangle button or steering wheel top button to open the wing.
  - Automatic (`F1CE_DRS=auto`): Wing opens automatically when entering eligible zones on straights.
- **Aerodynamic Effect:** Reduces high-speed drag, granting +18–22 km/h top-end boost and raising the vehicle speed ceiling from 280 km/h up to 345 km/h.
- **Safety Auto-Cancellation:** The wing closes instantly if the driver applies brakes (>8%), turns the wheel into a corner, or lifts off throttle.
- **Acoustic Feedback:** Double beep DRS activation chime synthesized at 48 kHz.

### Procedural Exhaust Acoustics & Rev Limiter

Real-time audio synthesis layered into the game's 48 kHz stereo audio stream (`F1CE_EXHAUST_POP=1`):
- **Overrun Crackles & Pops:** Procedurally generated exhaust pops and unburnt fuel detonations when rapidly lifting off throttle at high RPM (>12,500 RPM).
- **Downshift Barks:** Sharp throttle-blip exhaust detonation crackles on aggressive downshifts under braking.
- **Spark-Cut Rev Limiter:** Rhythmic cylinder ignition cut stutter and pops when bouncing against the 18,500–19,000 RPM ceiling.

### Dry compounds

Set `F1CE_TYRE_COMPOUND=soft` or `hard` (or legacy `F1CE_SLICK_COMPOUND`) before launch (or in `local.cmd`). Leave it unset for the baseline. This is a session-wide preset for eligible dry tyres, not an extra compound choice in the game's pit menu. It has no effect with stock physics.

| Compound | Wear rate vs. stock | Extra grip factor vs. the selected preset | Operating Thermal Window |
| --- | --- | --- | --- |
| Baseline | ×1.25 | ×1.00 | 80–102 °C |
| Soft | ×1.55 | ×1.04 fresh, falling to ×0.92 with wear | 85–105 °C |
| Hard | ×0.90 | ×0.98 fresh, falling to ×0.965 with wear | 95–115 °C |

The grip curve uses each wheel's own wear value and saturates at `0.65` internal wear units. The factors are initial tuning targets, not measured stint lengths.

### Telemetry

Enable **Physics telemetry (CSV)** or set `F1CE_TELEMETRY=1` (or legacy `F1CE_SLICK_TELEMETRY=1`) before launching a script. The game writes `slick_telemetry.csv` in its working directory (`handling` for the scripts) at approximately 10 Hz while the player drives. Stock physics can log too. Each run with driving samples overwrites the previous CSV; rename it before comparing runs.

The CSV includes speed in km/h, pad steering command (not road-wheel angle), throttle, brake input, gear, contact and wheel slip, wheel load, internal wear, tyre temperatures (`temp_0`..`temp_3`), brake disc temperatures (`btemp_0`..`btemp_3` in °C), lateral and longitudinal G-forces (`g_lat`, `g_long`), DRS state (`drs`: 0=closed, 1=available, 2=open), raw tyre flags, the current wear mode, physics eligibility, and whether the player is driving. Career dry sessions use base tyre flags `0x19` and wear mode `1`; Quick Race uses wear mode `2`. The transient `0x3000` status bits are ignored. It also samples the player car during hand-offs, such as pit-lane control; `player_driving` distinguishes those rows. Wheel indices 0–1 are front; 2–3 are rear.

To compare Stock and 2026, keep the car, track, assists, fuel load and weather the same. Mark the same corner and start/finish line crossings on each CSV's elapsed-time scale. For example, from the repository root:

```text
python handling/tools/compare_telemetry.py stock.csv modern.csv --stock-corner 45:48 --modern-corner 43:46 --stock-lap 20:110 --modern-lap 20:109
```

Repeat the lap options for a series of complete laps. The tool reports end-of-corner speed, the percentage of powered samples with rear wheelspin and both rear tyres in contact, and median lap time. Window boundaries are supplied by the driver, not inferred by the tool.

## Verification

Dry Silverstone runs confirmed activation of the grip, wear, and drive changes in `f1ce_recomp.log` (look for `slick test:`). The texture run confirmed all three replacements in `offline.wad`, and the smooth tread was visible on track.

A dry Silverstone Quick Race after the career eligibility fix confirmed W11 grip, wear ×1.25, and progressive drive assistance. Telemetry marked the dry career mode (`0x3019`, wear mode 1) eligible, while mode 3 remained ineligible.

`handling/tools/check_tuning.cpp` is a standalone C++20 check of the steering curve, boost ramp, wake physics, and compound curves. It is built as part of the project test targets (`check_tuning`). Run it to verify the tuning math without starting the game.
