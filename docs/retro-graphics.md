# Retro graphics: first pass

The retro graphics support adds a 720p composed picture, a separate world-height
setting, and nearest-neighbor enlargement. These are opt-in; existing defaults
remain unchanged. Compiler syntax checks cover the current changes; no automated
or game tests were run by the coding agent. The player has provided runtime logs.

## Settings

In the Screen tab, choose HUD / picture resolution 720p, Upscaling Nearest
(pixelated), and World resolution 480p, 360p or 240p. Restart after changing the
picture resolution; world resolution now changes live after startup. The world
setting is available on builds with the Ocarina widescreen patch sets. DLSS and
FSR 3.1 use their own scene scales and take precedence over the fixed world height;
the menu disables that height choice while temporal upscaling is selected.

The game draws its HUD and menus over the enlarged world at the picture's chosen
resolution, then enlarges that picture to the fullscreen window. Internal sizes
do not resize the window or switch the desktop's resolution. Aspect fitting and
black bars still follow the existing aspect setting. The host's F1 settings menu
continues to use its own display-sized rendering.

`world_res = game` preserves the existing scene-scale behavior, including the
legacy `scene_share` override. A numeric world height overrides that share. The
fixed world scale is applied after 120 presents so the game's resolution controller
can initialize. The startup picture can therefore use a different world size.
Widths and potentially heights remain subject to the engine's alignment rules.
The full performance overlay now shows the observed G-buffer as its scene size,
independently of temporal AA. Its game-image line includes the HUD. The requested
world setting is not presented as a measurement; video-only frames can omit the
scene row. A pending picture-resolution change is not reported as already active.

Nearest sampling is applied in the replacement scene decode-and-scale pass and
in the final presentation blit. The scene's color decode remains intact. Its
fallback blit also honors nearest sampling. Other game-owned post-processing
passes can still filter the world; this is not a replacement for the entire
post-processing chain.

## Suggested starting configuration

Merge the keys from `nx2navi-retro.ini.example` into the runtime's `nx2navi.ini`
beside the rebuilt executable, preserving other preferences. It requests 720p UI,
480p world, nearest scaling, low texture detail, BC3 texture compression and a
30 fps limit. Optional enhanced shadows and temporal reconstruction are disabled.
Many of those features were already disabled by default.

360p-to-720p enlargement is 2x and 240p-to-720p is 3x. 480p-to-720p is 1.5x and
produces uneven pixel sizes, as can the final enlargement to the display. This
pixelation is intentional.

## Expected results and remaining work

The lower world size should reduce scene pixel shading and bandwidth; lower
picture resolution also reduces some composition work. Low textures and BC3
primarily reduce memory pressure. Actual frame-rate and memory gains are unknown.
The player's log confirms 427x240 scene targets with a 720p composed picture;
visual correctness and performance across other scenes still need evaluation.

This pass uses the existing `fxaa = 0` behavior: its known-shader bypass may leave
AA in a required scaling pass, and shader changes can prevent recognition. Full
AA removal needs a later dedicated pass. Grass removal, earlier LOD, smaller or
fewer shadow cascades, and removal of volumetric fog, bloom and depth of field
are also deferred. The game-memory floor and hardware requirements remain.

## Cutscenes and the low-end capture

The player's November 11 log contains 427x240 colour/depth passes at 720p picture
resolution. This confirms a low-resolution world reached that run. Previously,
the world-height setting was cached until restart, so changing 240p to 480p during
the same run did not change that ratio. The live-setting implementation fixes
that cache and uses the active picture size while a different picture setting
awaits restart. Other game-owned filters can still soften the world before the
HUD and fullscreen enlargement.

The opening prerecorded movie is a separate 1920x1080 AV1 stream, not a 4K world
render. World resolution does not reduce that decoder's source dimensions. The
log measured about 56 to 149 ms per converted frame during the movie; the CPU
debanding path was active. Turning off Cutscene gradient smoothing removes its
per-pixel filter, high-precision temporary planes and conversion stripe workers
when the optional host overlay is off. The fast path still copies/interleaves
decoded samples into the game's full-size NV12 buffer. It also handles allocation
failure by dropping the picture rather than terminating the game.

Turn off Cutscene film grain to disable optional AV1 grain synthesis when present.
Restart for that decoder setting. Both switches default on, preserving the
existing path, and the additional profiles turn them off. The movie-source row
shows decoded dimensions. Actual lower-resolution AV1 decoding is not implemented;
lowering the final display size alone would not remove full-resolution codec work.
Real-time 3D cutscenes use the world's scene scale, with their geometry, skinning
and other CPU costs unchanged.

These changes keep every video timestamp and frame. They do not lower movie_fps,
which also affects the game's scripted narration/event timing. Conversion should
be substantially cheaper; total playback speed still depends on decoding and the
game's render/update loop and requires the player's evaluation.

The same log ends with an access violation inside igvk64.dll after the movie,
amid newly compiled 427x240 scene draws. That establishes a driver-module fault,
not its cause. This pass does not claim to fix it. Pipeline/shader waits during
that transition also differ from the steady-state overlay showing no waits.

`nx2navi-minimum.ini.example` adds a 240p world / 720p HUD starting profile with
the optional Intel compatibility layers, low textures, and the movie CPU filters
off. Merge keys into the existing settings file; do not replace input bindings
or save data. No assets need to be extracted again for these runtime changes.
