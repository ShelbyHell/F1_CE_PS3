ENVIRONMENTS - your own track lighting
======================================

Each folder here is a preset. It shows up in the launcher (General) in both
"Race environment" and "Time Trial environment".

A preset changes only the values it lists. Everything else comes from the
track's own file, so each track keeps its own sun direction, sky rotation and
crowd settings unless you change them.

Files in a preset folder (all optional):

  Preset.txt               Base=afternoon   start from the race light (blue sky)
                           Base=sunset      start from the Time Trial light (dawn sky)
                           (no Base: the session's own light)
  EnvironmentSettings.txt  sun, ambient, fog, HDR, crowds, sky values
  PostEffects.txt          bloom, god rays ("Crob Rays"), depth of field,
                           picture brightness / contrast / saturation
  <track>/EnvironmentSettings.txt, <track>/PostEffects.txt
                           values for one track only, applied last. Folder
                           names as in the game's tracks folder: bahrain, brazil,
                           canada, france, hockenheim, hungary, indy, istanbul,
                           jerez, malaysia, melbourne, monaco, monza, nurburgring,
                           sanmarino, shanghai, silverstone, spain, suzuka

One value per line, as in the game's own files:

  "Lights.Sun Colour"=8.0 6.0 4.0
  "Picture.Saturation"=0.85

Lines starting with //, # or ; are comments. Colours are R G B and can go above
1 (the game renders in HDR). Directions are X Y Z. Light changes when a track
loads: restart the session to see an edit.

The game's own files are in your game folder:
  PS3_GAME/USRDIR/tracks/<track>/EnvironmentSettings.txt      race (afternoon)
  PS3_GAME/USRDIR/tracks/<track>/PostEffects.txt
  PS3_GAME/USRDIR/tracks/<track>/tt_EnvironmentSettings.txt   Time Trial (sunset)
  PS3_GAME/USRDIR/tracks/<track>/tt_PostEffects.txt

EnvironmentSettings.txt keys, with Silverstone's race values:
  "Lights.Sun Direction"=40 -70 -20         sun direction (the light goes this way)
  "Lights.Sun Colour"=10 9.9 9.75           sunlight colour and strength
  "Lights.Sun Billboard Colour Adjust"=1 1 1
  "Lights.Ambient Colour"=2.2 2.15 2.05     sky light in the shadows
  "Lights.EnvMap Ambient Colour"=0.1 0.1 0.1
  "Lights.EnvMap Scalar"=5                  strength of the car reflections
  "Lights.EnvMap Sky Image Brightness"=1
  "Ambient occlusion.Brightness"=1
  "Ambient occlusion.Contrast"=1
  "Ambient occlusion.Billboard Brightness"=1
  "Ambient occlusion.Billboard Contrast"=1
  "Ambient occlusion.Direct Lighting Hack"=1
  "Atmospherics.Fog Colour"=6 6 4
  "Atmospherics.Fog density"=0.0003         0.001 is the Time Trial haze
  "HDR.Adaption Rate"=0.25                  how fast the exposure adapts
  "HDR.Fixed Geometry Luminance"=0.5
  "HDR.Fixed Sky Luminance"=2
  "HDR.Fixed Sun Luminance"=4
  "HDR.Sky Image Brightness"=14             brightness of the sky picture
  "Crowds.Y Offset"=0.15
  "Crowds.Happiness"=0.5
  "Crowds.Excitedness"=0.5
  "Crowds.TexelSize"=0.014
  "Crowds.Falloff Start"=80
  "Crowds.Falloff End"=600
  "Crowds.Falloff Angle Bias"=0.6
  "Sky.Rotation"=-50                        turns the sky picture (degrees)
  "Sky.Y Offset"=0
  "Sky.Reflection Y Offset"=-4000
  "Sky.Reflection Tint"=1 1 1
  Also read by the game, not in the track files: "Lights.Shadow filtering spread",
  "Lights.Shadow near push proportional", "Lights.Shadow near push absolute",
  "Lights.Shadow far push absolute", "HDR.Fixed EnvMap Luminance",
  "Crowds.High Quality", "Sky.Wing Mirror Y Offset".

PostEffects.txt keys, with the race values:
  "Motion Blur.Enable"=1                    (the launcher's Motion blur option wins)
  "Depth Of Field.Enable"=0
  "Depth Of Field.Focal Distance"=612.21
  "Depth Of Field.Focal Near"=-10000
  "Depth Of Field.Focal Far"=912.21
  "Depth Of Field.Blur Ammount"=0.5
  "Bloom.Enable"=1
  "Bloom.Colour Scale"=0.125
  "Bloom.Size"=1 0.5
  "Crob Rays.Enable"=0                      god rays (1 in Time Trial)
  "Crob Rays.Colour Threshold"=0.7 0.7 0.7
  "Crob Rays.Colour Scale"=0.1 0.1 0.1
  "Crob Rays.Distance Scale"=0.2
  "Rain And Muck.Enable"=0
  "Rain And Muck.Rain Refraction"=0
  "Alpha To Coverage.Filter Offset"=0.5
  "Picture.Brightness"=1
  "Picture.Contrast"=1
  "Picture.Saturation"=1

The example presets are rebuilt with the launcher. Copy one to a new folder
before editing it.
