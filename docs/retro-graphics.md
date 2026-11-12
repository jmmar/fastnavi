# Retro graphics: first pass

The `retro-low-spec` branch adds a 720p composed picture, a separate world-height
setting, and nearest-neighbor enlargement. These are opt-in; existing defaults
remain unchanged. No build, automated tests or runtime validation have been run.

## Settings

In the Screen tab, choose Resolution 720p, Upscaling Nearest (pixelated), and World
resolution 480p, 360p or 240p. Restart after changing either resolution. The world
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
The resolution conversion below the previous 1080p minimum is inferred from the
existing 1080p path and needs the user's in-game validation.

This pass uses the existing `fxaa = 0` behavior: its known-shader bypass may leave
AA in a required scaling pass, and shader changes can prevent recognition. Full
AA removal needs a later dedicated pass. Grass removal, earlier LOD, smaller or
fewer shadow cascades, and removal of volumetric fog, bloom and depth of field
are also deferred. The game-memory floor and hardware requirements remain.
