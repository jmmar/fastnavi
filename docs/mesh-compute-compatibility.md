# Optional mesh/task compatibility layer

`compat_mesh = 1` selects a separate compute-to-geometry implementation. The native
task/mesh generator, mesh pipelines and mesh draw commands remain the default.
This increment is experimental and has not been run with game shaders or gameplay.

## Building and selecting it

The translator build includes `tools/xlate/MeshComputeSpirv.cs`. Rebuild the
translator with `tools/build_xlate.py` and package/rebuild the runtime from this
checkout. The translator was compiled during this increment; existing setup EXEs
and installed games still hold their older code.

On the current Intel computer, both layers are needed before shader preparation:

```ini
[compatibility]
compat_bda32 = 1
compat_mesh = 1
```

`nx2navi-intel-compat.ini.example` includes those choices and a 720p picture with a
360p world as a starting configuration. Merge it into the game's INI, preserving
other preferences. The release packager carries the examples in `app/<version>`;
it does not overwrite the game's existing settings to activate them.

For a first install without an INI, close the launcher and game completely, then
start the setup from a PowerShell session with both overrides:

```powershell
$env:NX2NAVI_COMPAT_BDA32 = '1'
$env:NX2NAVI_COMPAT_MESH = '1'
& '.\build\retro-release\NX2navi-Setup-0.7.2-retro.2.exe'
```

That setup must first be packaged with version `0.7.2-retro.2` or another new
version. The overrides are inherited by the launcher and shader preparation.
Returning to an already running launcher would retain its old environment.
Remove the overrides with `Remove-Item Env:NX2NAVI_COMPAT_BDA32` and
`Remove-Item Env:NX2NAVI_COMPAT_MESH` when no longer needed.

## Data flow

1. The existing instruction lowering still produces a compute representation of
   each task or mesh program. The optional converter wraps that function instead
   of turning it into native task/mesh stages. Both entries retain their original
   uniform, storage, texture, image and address-table resources.
2. Task compute writes one bounded payload record and mesh-group count for each
   parent task group. Guest workgroup IDs are adjusted for tiled dispatches.
3. Mesh compute reads its parent's payload, executes the lowered mesh function,
   and expands indexed points, lines or triangles into vertex records. Position,
   point size and generic varying vectors are copied from the existing stage
   memory layout. A shared atomic counter reserves output space and becomes the
   conventional indirect draw's vertex count.
4. A generated vertex-pulling shader reads the records and passes their values to
   the existing fragment shader. Direct and indirect mesh draw entry points route
   here only when the option is selected.

The wrapper uses SPIR-V 1.3 compute and vertex entry points, with no task/mesh
execution models or mesh-shading extension. It remains independent of the paired
32-bit address option; that second option is necessary where shaderInt64 is absent.

## Runtime and ownership

`runtime/vk_mesh_compat.inc` contains the additional buffer management and dispatch
helpers. `runtime/gfx_shader.c` owns compute and vertex companions with each parent
translation, including temporary private shaders used during preparation. Their
SPIR-V is carried in the existing shader/reflection cache pair, with a mesh ABI tag.
No new game assets or extracted content are stored in source control.

One extra storage descriptor names the compatibility buffer. Compute descriptors
are resolved against the original task/mesh banks rather than the game's compute
banks. Generated vertex shaders use the existing graphics pipeline and resource
binding infrastructure. Generated companions are not written into pipeline-key
records that would otherwise mistake them for loadable original game programs.

Geometry buffers are reused within a command-buffer fence and released after GPU
completion. Each batch is at most eight task groups, reduced further to fit a
32 MiB target and the device's storage-buffer and dispatch limits. A larger
layout can allocate a replacement buffer within the same fence; earlier buffers
remain alive until completion. Barriers separate transfers, task compute, mesh
compute, indirect reads, vertex reads and reuse by a subsequent batch.

## Limits and expected results

- Indirect draws are bounded to 4096 parent task groups. The actual count and first
  group stay on the GPU. An excessive count omits the entire draw; a completion
  callback logs it. It is not silently truncated to partial geometry.
- Vertex and primitive limits are at most 256; workgroups at most 1024 threads.
  Actual shared-memory and per-workgroup device limits still apply. A task/mesh
  pair with inconsistent payload layouts is rejected.
- A single task group's maximum expanded output must fit the buffer budget.
  Unsupported or missing metadata, allocation failures and pipeline failures omit
  the affected draw and log the failure rather than using invalid native stages.
- Indirect draws issue bounded batches even when many contain no active work.
  This can add substantial CPU and GPU overhead. Expanded vertices also cost more
  bandwidth than native mesh output. Playable speed on the current Intel GPU is
  unknown.
- Atomic allocation can change the order of geometry from different mesh groups.
  Order-sensitive blending, task payload interpretation, coordinate handling,
  output attributes and depth/shadow agreement need user validation.
- System-memory demands and the game's large application pool are unchanged.
  Feature compatibility does not establish that the machine has sufficient memory.

No unit tests, generated shader test cases, shader preparation or game execution
were performed. Compilation checks only establish that the implementation builds;
they do not establish that generated SPIR-V passes driver validation or renders
correctly. Inspect the user's resulting logs before extending or enabling this
path by default.
