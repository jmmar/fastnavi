# Integrated GPU compatibility

## Current computer

Driver capability query, November 11, 2026:

| Capability | Reported value |
| --- | --- |
| Device | Intel HD Graphics Gen11 |
| Vulkan API | 1.3.215 |
| shaderInt64 | false |
| VK_EXT_mesh_shader | absent |
| bufferDeviceAddress | true |
| Required startup extensions | all present |
| Other explicitly required startup features | all present |
| Device-local memory heap | 7.92 GiB, shared integrated-GPU memory |
| Host pointer alignment | 4096 bytes |

The required-feature comparison follows `runtime/vk_device.c`, `create_device`,
and the extension comparison follows `k_required_ext`. A passing capability
comparison alone does not verify presentation, memory allocation or rendering.

Raw output and the summarized JSON are under `build/gpu-report`. Driver warnings
about missing overlay layer manifests did not prevent this query. The report
does not contain game data or keys. No game execution or benchmarks were performed.

## Collecting the N150 report

Copy `tools/gpu_report.ps1` to that computer. It needs PowerShell and
`vulkaninfo.exe`, usually supplied with Vulkan Tools. From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\gpu_report.ps1 -OutputDirectory .\build\n150-gpu-report
```

If Vulkan Tools is outside PATH, add `-VulkanInfo "C:\path\to\vulkaninfo.exe"`.
The script can also be copied and run by itself with an explicit output directory.
Bring back `capabilities.json`, `vulkaninfo.txt` and `vulkaninfo-errors.txt`.
It queries the driver without opening the game, compiling game shaders or running
test cases. An unreported feature is recorded as null, not assumed supported.

## Implementation sequence

Two optional layers are implemented, without confirmed Intel gameplay support.
`compat_bda32 = 1` under `[compatibility]` selects a separate
`DeviceAddress32` generator. With the default 0, the native generator and startup
requirements remain active. `compat_mesh = 1` additionally selects compute-based
task/mesh execution and vertex pulling; its default 0 retains native mesh shaders.

The implementation is in `tools/xlate/compat/DeviceAddress32.cs`; patch 0039 adds
the optional dispatch and capability flag in the build copy of the shader source.
The vendor tree and existing patch files remain untouched. It uses the existing
four-word address-table records, unmapped-page dummy, twin scratch and shared
load/store/atomic emission. Address arithmetic propagates carry between low and
high words; pointer construction uses the permitted physical-pointer bitcast from
two 32-bit words.

Translator jobs carry `bda32: true` only for this option. Both the translator and
the runtime reject modules still declaring Int64 or 64-bit integer types, rather
than submitting them to an incompatible driver. A stale translator can therefore
fail explicitly instead of silently using an unsupported native module. The
runtime-owned `gpa_read.spvasm` remains the native diagnostic shader; it is not a
gameplay shader and its diagnostic is not part of this compatibility layer.

Shader keys include the selected layers. With both enabled, pipeline caches use
`pipeline-cache-bda32-mc1.bin`, and binaries use `pipeline-binaries-bda32-mc1.bin`.
Pipeline keys and compute-use records are separated too. A user-specified pipeline
cache path receives a `.bda32-mc1` suffix. Address-only mode uses `bda32`; mesh-only
mode uses `mc1`. Existing native cache files are retained.
The shared shader-cache inventory can contain both sets of keyed shader entries;
it is not a separate copy of the whole shader-cache directory.

For a user-run experimental attempt after rebuilding the translator and runtime,
add this to the game's INI **before** shader preparation or game startup:

```ini
[compatibility]
compat_bda32 = 1
compat_mesh = 1
```

For an installation that has not made an INI yet, create it beside `ocarina.exe`
or set both `$env:NX2NAVI_COMPAT_BDA32 = '1'` and
`$env:NX2NAVI_COMPAT_MESH = '1'` in the PowerShell session used to start the
setup/launcher. Fully close the existing launcher and game first. Child processes
inherit those choices. Remove the environment variables to return to INI/default
selection. Do not disable `gpa`.

The address option permits an alternate path through the shaderInt64 gate; the
mesh option supplies additional translation and draw submission. Other startup
gates remain. Neither compilation nor C syntax checks establish shader correctness,
complete rendering or driver compatibility. User runtime validation is required.
See `mesh-compute-compatibility.md` for the implemented path and its limits.

Compilation status for this increment: the translator NativeAOT build succeeded,
and the changed C sources passed compiler syntax checks. No unit tests, shader
test cases, shader preparation, full game build or game execution were performed.
The newly built translator is under `build/xlate`; an already packaged setup or
installed game does not receive these source changes until it is rebuilt.

1. **Address-layer design.**
   Start with the device-address generation in `tools/xlate/patches/0030-global-memory-through-device-addresses.patch`.
   It currently assembles addresses in a 64-bit integer and declares Int64.
   Investigate paired 32-bit arithmetic and a physical-pointer representation
   that does not require Int64, checking the permitted SPIR-V operations before
   implementing it. Audit all translated and runtime-owned shaders for remaining
   Int64 declarations and operations, including `runtime/shaders/gpa_read.spvasm`.
   Preserve address carry, bounds behavior, unmapped-page handling, atomics and
   mirrored writes. Do not simply remove the device check or disable GPU global
   memory access: that loses game state. A complete fallback must select the
   correct shader variants and cache identity before relaxing the startup gate.

2. **Mesh/task design.** `tools/xlate/AmpereLowering.cs` and
   `tools/xlate/MeshSpirv.cs` already have a lowered compute representation and a
   stage-buffer layout. Design a compute path that writes vertex/index buffers
   and indirect draw arguments, then feeds a conventional vertex/fragment pipeline.
   Preserve task payloads, emitted group counts, vertex attributes, primitive
   counts and instance data. The runtime needs scratch-buffer lifetime management,
   compute-to-draw barriers and direct/indirect draw handling. The current behavior
   of dropping unsupported mesh draws is not a usable compatibility mode.

3. **User validation, then performance reductions.** Once the complete renderer
   can initialize and draw gameplay on the target driver, the user validates it.
   Then evaluate the 720p picture with 480p, 360p or 240p scene choices. Reduce
   vegetation, shadows and effects after the rendering path works. Shader feature
   compatibility does not guarantee sufficient memory or playable frame rates.

This work is a renderer compatibility project, separate from the graphics preset.
No unit tests or automated game tests will be written or run. Runtime validation
belongs to the user. The next step is user validation of the two optional layers
on the current computer. Compare the N150's
actual driver report before assuming it has the same missing features.
