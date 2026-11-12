# Graphics diagnostics and compatibility synchronization

The next player run can collect useful graphics data without dumping textures,
game buffers or shader contents. No game execution, automated tests or synthetic
test cases were performed while implementing these changes. Compiler and packaging
checks are separate from runtime validation.

## Settings

Set these in nx2navi.ini beside the game executable, then restart:

```ini
graphics_diag = timed
```

The choices are auto, off, on and timed. Auto enables bounded CPU history and
workload summaries when either compatibility layer is configured; it leaves
ordinary native rendering diagnostics off. On explicitly enables that history.
Timed also enables the existing GPU timestamp profiler. An explicit gpu_passes
setting takes precedence over the timing part, so gpu_passes = 0 still disables
GPU profiling. The Screen menu exposes the same choices.

Keep existing input, save, display, movie-skip and recorder settings. There is no
need to replace the settings file or re-extract assets. Low texture detail was
confirmed in the player's last run and is not being suggested again as a new fix.

## What is recorded

runtime/vk_diag.c keeps the last 64 CPU-side records in a fixed array. Fields are
atomic, and writers skip a busy slot rather than wait. The fatal-path snapshot
does not take renderer locks, allocate GPU resources, read mapped GPU buffers,
wait for the GPU, or call Vulkan. It is a best-effort record of CPU recording
activity; it does not establish that the GPU reached or completed an operation.

An access violation, a fatal runtime error, or a reported device loss appends the
history and process/system memory commit to the normal nx2navi.log, once per run.
The history includes:

- Frame, submission fence when known, command-buffer handle and operation name.
- Fill-buffer handles, offsets, byte counts and fill patterns; the mesh and guest
  fill sites also record their allocation/backed-span bounds. The recorder records
  the actual driver call separately from the producer's issued command.
- Compatibility buffer allocation and fence-completed header observations. The
  latter describe the last draw using that buffer, not all its draws.
- Mesh program, batch base, group count and capacity, plus companion compute IDs.
- Render-target width/height, primary colour format, and depth layer.
- Vertex/fragment/compute shader cache-key prefixes. These identify variants in
  the shader cache; they are not the separate SPIR-V hashes used by GPU pass logs.

Every five seconds, and after a frame longer than 500 ms, a short summary reports
draws, guest dispatches, passes, compatibility batches, group upper bounds and CPU
recording/wait time. Counts cover the stated interval. CPU recording/wait time
includes waits for shader/pipeline work; it is not GPU execution time. Long
pipeline-wait messages gain program and shader identifiers and target dimensions.

Timed mode uses runtime/vk_crumbs.c's existing timestamp capture: four frames every
240 presents, reported later when results are available. Compatibility compute
dispatches now participate in those marks too. Captures can miss a short transition,
and a device lost before query results become available can prevent a timing report.
Profiling adds overhead; graphics_diag = on retains CPU diagnostics without it.

## Concrete synchronization correction

runtime/vk_mesh_compat.inc, mesh_compat_generate writes its indirect draw header,
clears task payloads and may copy GPU-produced indirect arguments before the
task/mesh compute dispatches. Those transfer commands did not increment recorded.

runtime/vk_exec.c, barrier_to uses recorded to skip a barrier when no work has
occurred since the previous one. The missing increments could therefore suppress
the transfer-to-compute barrier. Compute could observe stale counts or payloads,
which is unsafe for generated geometry. The transfer commands now increment the
counter before the next barrier. Native mesh rendering is unchanged. This fixes
a concrete ordering defect; whether it explains the player's device loss remains
to be established by the player's run.

## Optional larger batches

```ini
compat_mesh_batch = auto
```

The default 8 preserves the previous cap. Auto removes that arbitrary cap while
retaining the existing 32 MiB geometry-buffer budget, metadata checks and GPU
dispatch limits. It may use a larger temporary buffer and reduce CPU recording,
barriers and render-pass restarts when metadata allows more than eight groups.
It does not remove geometry or eliminate processing of the indirect upper bound.
Small hardware/memory limits can leave batch size unchanged. The Screen menu
offers Standard and Larger batches. Restart after changing this setting.

## Latest pre-change failure and fixed-size effects

The player's November 11 run used movie_skip = 1, vk_recorder = 0, and
texture_detail = low. It reached initial 427x240 world passes, then reported a
lost device during an upload submission. One transition frame took about 28 s,
including about 23 s attributed to pipeline building. The process committed about
13.1 GB at failure. CPU compilation stalls, memory pressure and GPU execution
must be distinguished; a long CPU pipeline build alone does not prove a GPU
watchdog timeout. This differs from the earlier access violation in the recorder.

Lower-resolution effects are potential optimizations, but a global texture resize
is unsafe. Shadow maps, froxel volumes, bloom chains and composition targets can
have independent size constants, dispatches, mip chains, aliases and consumers.
The current game's patch sets do not provide a universal effects-size control.
The new target/shader history and GPU pass timings help identify which passes
are expensive before adding a paired producer/consumer change. The 720p HUD and
picture do not need to be reduced to investigate smaller world effects.

The following [low-memory profile](low-memory-profile.md) adds optional smaller
allocator/cache policies, safe staging-allocation retries and per-category memory
totals to the fatal snapshot. Its standard setting preserves the previous policies.
