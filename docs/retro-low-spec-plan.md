# Retro graphics implementation plan

Status: first-pass source changes prepared; no build or runtime validation. Branch: `retro-low-spec`.
See `retro-graphics.md` for the implemented settings, preset and remaining limitations.

## Target presentation

Render the game world at 480 lines or fewer, compose the HUD and menus at 1280x720,
and enlarge the composed picture into the existing fullscreen window. The monitor
mode and window size remain independent of these internal resolutions. Pixelation
and uneven pixel widths at non-integer enlargement factors are acceptable.

Offer world heights of 480, 360 and 240 lines as initial targets. Width follows the
picture aspect ratio and the engine's alignment requirements. A 480-line 16:9 world
is approximately 854 pixels wide; the actual aligned size must be reported rather
than assumed. Keep the current graphics behavior as the default and make the retro
options explicit and reversible.

This plan is based on source inspection. Runtime behavior and performance are not
verified. Do not write or run unit tests or automated test cases for this work.
The user will perform runtime validation. Do not claim measured improvements.

## Existing mechanisms and limits

- `runtime/widescreen.c`, `picture_height`, clamps the composed picture to 1080
  lines. `picture_size` and `wide_init` already separate the picture size from the
  scene size through the Ocarina patch page.
- `runtime/widescreen.c`, `scene_share` and `wide_present`, already control the
  scene scale with `res_fixed`. The plain scene share is cached at first use;
  a new menu setting must deliberately handle updates. The fixed path is enabled
  after 120 presents so the game's resolution controller initializes first.
- `recomp/patches-v111.txt` has output, scene and fixed-scale sites. Its scene-size
  constants alone do not reliably control all scene targets; use the established
  resolution controller path. Behavior below a 1080-line picture needs user
  confirmation, rather than assuming the existing scale conversion generalizes.
- `runtime/vk_tonemap.c`, `tm_init`, uses linear filtering to enlarge the world
  before the HUD is composited. It also decodes the stored scene color. Preserve
  that color conversion when adding nearest-neighbor sampling.
- `runtime/gfx_present.c`, `put_picture`, applies FSR or a linear blit to enlarge
  the composed picture. Pixelated presentation requires controlling this second
  enlargement too.
- `runtime/vk_dlaa.c`, `vk_dlaa_skip_draw`, disables recognized FXAA draws but
  preserves a combined scaling draw where its output is required. A reliable
  no-AA option must account for this and shader identification changes.
- `runtime/cfg.c`, `runtime/ui/menu.cpp`, `runtime/ui_text.c` and its header, and
  `nx2navi.ini.example` need consistent descriptions and choices.

## Implementation order

1. **Separate UI resolution and world resolution.** Add 720p composition support
   and an explicit world-height setting for the Ocarina path. Preserve the default
   resolution and upscaler choices. Define how explicit world height interacts
   with DLSS, FSR 3.1, dynamic resolution and the legacy scene-share override;
   retro mode should use a fixed world size with temporal reconstruction off.
   Keep the controller's initialization sequence and report requested versus
   actual sizes. Leave display resolution and fullscreen window dimensions alone.

2. **Pixelated enlargement and AA removal.** Provide nearest-neighbor world-to-UI
   and picture-to-window scaling. Keep the color decode, aspect handling, crop,
   HUD composition and movie handling intact. Make a complete no-AA path without
   dropping a required scene copy or scaling pass. Preserve ordinary bilinear and
   FSR modes for existing configurations.

3. **Cheap graphics preset.** Combine low texture detail, no temporal upscaling or
   frame generation, no optional contact shadows or PCSS, and the new scaling
   choices. Offer a separate 30 fps choice. Several of these features are already
   off by default, so do not count them as additional savings from defaults.

4. **Reduce foliage shadows.** Extend the existing foliage shadow classification
   in `runtime/vk_exec_draw.c` (`foliage_shadow_cull`, `mesh_shading_rate`) with a
   selective off option. These are broad mesh/shadow heuristics, so identify their
   coverage before treating them as an exact grass or tree classifier. Add smaller
   shadow targets or shorter shadow ranges only with Ocarina-specific support:
   its current patch table lacks the shadow-size and cascade-range sets used by
   other versions. `shadow_scale` currently only enlarges maps.

5. **Reduce decorative grass and distant geometry.** Identify decorative grass
   independently of trees, terrain and interactive objects. Prefer reducing
   density, generation distance and LOD thresholds before submission, with an
   optional grass-free appearance. Keep collision and gameplay state intact.
   `runtime/vk_texture.c`, `vk_texture_write_back_small`, documents cut/trampled
   grass maps read by CPU gameplay code; hiding grass must not discard that data.
   Never use the global mesh-draw diagnostic switch as a grass setting.

6. **Simplify effects.** Identify bloom, depth-of-field and volumetric fog producer
   and consumer passes. Bypass effects with valid neutral inputs or a direct scene
   path, then avoid their unused upstream work. Replace volumetric fog with cheap
   distance fog where feasible; match the fog distance to reduced scenery range.
   Leave essential lighting, scene color conversion and UI fades functional.

## Expected benefits and limits

Low scene resolution reduces pixel shading and scene-target bandwidth. Keeping
the UI at 720p means some composition and presentation work remains at that size
or the display size. Removing vegetation and its shadows targets geometry and
alpha-test overdraw that resolution reduction alone cannot eliminate. Earlier
LOD and culling can also reduce submission work when applied before draws are
created. Low texture detail primarily reduces texture residency and decoding work.

480p-to-720p enlargement is 1.5x vertically, so nearest-neighbor pixels will have
uneven sizes. 360p-to-720p is 2x and 240p-to-720p is 3x. Final fullscreen scaling
may introduce another non-integer enlargement; this is accepted by the user.

Lower rendering demand does not remove the game's substantial application-memory
allocation, Vulkan requirements or mesh-shader dependency. Supporting older GPUs
without mesh shaders would be a separate renderer project.

## Handoff

Implement in separate, reviewable increments. Record changed settings, expected
visual behavior and unresolved assumptions for the user's runtime validation.
Do not write or execute automated tests. No build, commit or push has been performed.
