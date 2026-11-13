# Performance controls and the upstream update

## Foundation and retained work

This update starts from upstream commit `d4778b1bb0b8ef929a9b23c387a22f5cef5b4101`
(version 0.9.2), checked on November 13, 2026. It retains the current renderer,
shader translator, memory management, anti-aliasing, launcher and low-end GPU mode.
The original fork is preserved locally on `backup/before-upstream-2026-11-13`
at `ac0719e`. The update branch is `update/upstream-performance-dials`.

The additions carried forward are deliberately small:

- 720p composed pictures, alongside the existing higher resolutions.
- `world_res = game|240|360|480|720`, a separate requested scene height before the HUD.
- `upscaler = nearest`, available in the Screen menu and performance overlay.
- Two example configurations, packaged with the launcher but never applied automatically.
- Windows certificate support in the documented packaging command and a local build helper.

The old `compat_bda32` and `compat_mesh` implementation is preserved on the backup
branch, not enabled or carried into this runtime. These were compatibility experiments,
not general performance switches. Upstream now has its own mesh fallback and optional
mesh emulation, so keeping both would add competing translation and draw paths.
The old paired-word addressing layer is not replaced by mesh emulation: this foundation
still requires `shaderInt64`. Do not assume the previously investigated Intel Gen11
device can run this version. A future Intel port needs a separate, validated effort.

No lifter, module layout, vendor sources or game patches were changed for these dials.
The translator remains upstream's implementation and patch set.

## What is worth adjusting

These are source-based judgments, not benchmark results for this fork.

| Control | Purpose and tradeoff | Recommendation |
| --- | --- | --- |
| Frame limit | Reduces the number of frames the CPU and GPU must deliver; trades smoothness for headroom. | First control: baseline at 60, test 30 on a weak machine. Compare real frame times with generation off. |
| Picture resolution | Reduces scene, composition and HUD work together; text and HUD become less sharp. | 1440p baseline on RX 6900 XT; 1080p next, 720p for constrained hardware or the retro look. Restart. |
| FSR 3.1 Quality/Balanced | Reduces scene shading while retaining the picture/HUD size; adds reconstruction cost and possible motion artifacts. | First GPU experiment on the RX 6900 XT. Requires the FSR DLL. Quality before Balanced. |
| Fixed world height | Reduces scene work independently of HUD size, with coarse geometry detail and pixel edges. | Keep as an opt-in dial. Start 720 or 480, then 360/240 if desired. Temporal upscaling takes precedence. Restart. |
| Nearest scaling | Changes the appearance of enlarged pixels. Other post effects can still filter the image. | Keep for presentation; do not advertise it as a substantial performance improvement. |
| Texture detail | Reduces texture residency and upload/decode demand, at a visible detail cost. | Auto on the main machine. Reduced/Low when memory pressure causes missing textures or repeated uploads. Restart. |
| ASTC compression | Trades decode/compression work for lower texture memory use. | Keep upstream's automatic choice; do not force uncompressed textures on the 16 GB RX 6900 XT. |
| Anti-aliasing | Controls an additional screen-space pass; temporal reconstruction already supplies its own AA. | Keep upstream's Game default for baseline; try Off separately. The retro example uses the current `aa` control. |
| Shadows/contact shadows/PCSS | Larger maps and optional shadow effects cost GPU work and memory. | Baseline: `shadow_scale = 1`, contact shadows and PCSS off. Larger maps are a quality dial, not an optimization. |
| Anisotropic filtering | Improves surfaces viewed at an angle; cost depends on the scene and GPU. | Keep 16x on RX 6900 XT; try Game or 4x only after resolution and texture choices on weak hardware. |
| FSR frame generation | Makes extra displayed frames; does not accelerate game simulation or reduce input latency to that of the displayed rate. | Test after establishing adequate real frame rate and correct rendering. Keep off for diagnosis. |
| Replay workers/pipelining | Can reduce CPU submission bottlenecks; extra concurrency can expose timing, visual or stability faults. | Keep upstream defaults. Experiment with workers first, then pipelining separately; never change a group of knobs for the baseline. Restart. |
| Low-end GPU mode | Enables mesh emulation, memory-budget controls, staged uploads, asynchronous replay and other behaviors together. | Keep upstream implementation, off on RX 6900 XT baseline. Treat as an experimental bundle for weaker GPUs. Restart. |
| Foliage reduction | Removes some or all indirect mesh foliage draws; changes the scene visibly. | Useful weak-GPU experiment through low-end mode. It is gated by `low_end_gpu`; setting foliage alone has no effect with the mode off. |
| Guest optimization | `guest_opt = 2` builds generated code at higher optimization and takes longer. | Preserve upstream's option, not the default. It is gated by low-end mode and needs Rebuild the Game. Compare before adopting. |
| GPL and lazy write-back | Driver-sensitive pipeline and memory synchronization behavior. | Preserve upstream's AMD safeguards. Do not force `gpl = 1` or `wb_lazy = 1` into a generic speed preset. |

Lower resolution will not help much if draw replay or game simulation dominates frame
time. If Quality and Performance give almost the same real frame rate in the same scene,
stop sacrificing image quality and investigate CPU work instead. A fixed 240p world
does not remove the game's system-memory requirement or missing GPU feature requirements.

## Main-machine baseline

Hardware detected here: Core i7-10700K (8 cores, 16 threads), 32 GB RAM, Radeon RX 6900 XT.
The Vulkan query reported API 1.4.315 and native mesh shaders. This is not the Intel
Gen11 machine described by the old fork's compatibility notes.

Merge `nx2navi-rx6900xt.ini.example` into the `nx2navi.ini` beside the built game;
retain controls, save paths and other preferences. It starts at 1440p, 60 fps,
automatic texture detail, normal scene scaling, and no frame generation or
experimental concurrency. Leave driver-sensitive settings unset so upstream selects them.
It is a reproducible starting point, not a claim of optimal performance.

Next, try FSR 3.1 Quality through the Screen menu. Its ini equivalent under `[display]`
is `dlaa = 1`, `taa = fsr3`, `dlss = quality`, `fsr4 = 0`, `upscaler = bilinear`.
The `dlss` key holds the scale name for both temporal upscalers; these settings select FSR.
This build packages FSR 3.1, not FSR 4 or NVIDIA DLSS.

For a deliberately coarse presentation use `nx2navi-retro.ini.example` instead.
360 to 720 and 240 to 720 are integer enlargements; 480 to 720 is not. The final
enlargement to the monitor may also be non-integer. Requested scene dimensions are
subject to game alignment and scaling rules; below-1080p behavior needs gameplay
validation. The world-height row is disabled while temporal scaling is selected.

The Resolution row keeps upstream's behavior: it changes the picture after restart
and the window size immediately outside fullscreen. A fixed world height does not
change the window or desktop resolution.

## Build the local launcher

From PowerShell in the repository:

```powershell
.\tools\build_local_launcher.ps1
```

The helper finds MSYS2 or Vulkan SDK shader tools, rebuilds the translator, validates
runtime shaders, packages FSR 3.1 and uses `truststore` for Windows certificate
validation. It writes `build/launcher-release-updated`, separate from earlier packages.
Packages carry the local version suffix `-fastnavi.1` to distinguish them from the upstream release.
It does not publish or install anything and does not change an installed game's files.
Run the resulting setup yourself when ready to install. The launcher uses the fork's
release feed (`jmmar/fastnavi`), so upstream updates do not replace the custom runtime.

Prerequisites: uv, .NET 10 SDK, Visual Studio C++ build tools, and either MSYS2 shaderc
and spirv-tools or the Vulkan SDK. See `building.md` for the toolchain.
The first build needs network access and downloads the launcher's pinned compiler,
runtime and Python dependencies. The helper expects the signed SDK 1.1.4 FSR library
at `build/deps/amd_fidelityfx_vk.dll`; another location can be passed with `-FfxPath`.
The official source is the [SDK 1.1.4 release](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v1.1.4).
No game files are needed to package the launcher. Building and playing the game require
the user's own dump through the launcher.

After an upstream update, rebuild the translator; `-SkipTranslator` is only for repeated
packaging of this same source version. Do not reuse the old fork's translator, prebuilt
runtime or prepared shader package as the implementation of this update.

## Validation and adoption

Build and content-check results are recorded in `build/upstream-update-validation.md`.
No game run or measured performance gain is implied by a successful compilation.
The new low-resolution paths need the user's visual validation.

For manual comparison, use the same save, route, camera, resolution, frame limit and
background applications. Let shader preparation complete and traverse the route once
before recording. Compare at least three runs of the same route, including frame-time
spikes and memory use, not just the highest displayed FPS. Include a town, Hyrule Field,
combat, the pause menu and a restart. Check HUD scaling, water, foliage, shadows and
texture stability. For temporal scaling include camera turns and area transitions.
Change one dial at a time; restore the baseline if the game loses its device or displays
new artifacts. Test both machines independently before describing a dial as faster.

Implementation references: `runtime/widescreen.c` (`picture_height`, `scene_share`),
`runtime/gfx_present.c` (`put_picture`), `runtime/vk_tonemap.c` (`tm_init`,
`vk_tonemap_record`), `runtime/cfg.c` (`metas`, `LOWEND`, `LOWEND_ONLY`),
`runtime/ui/menu.cpp` (`row_resolution`, `row_world_res`, `row_upscaling`),
`runtime/vk_caps.c` (driver defaults), and `tools/make_game.py` (build options).
