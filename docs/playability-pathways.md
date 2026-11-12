# Measured playability pathways

The player's retro.5 log reaches several minutes of world rendering and then
reports device loss while submitting four held submissions. The steady sample
averages about 2.7 FPS, with most long frames attributed to fence waits. Low
memory mode was active. At failure, staging had no open or in-flight batch-owned
buffers; about 30 MiB was cached/idle. The recorded staging peak was 382 MiB,
and compatibility geometry had a 51.7 MiB live peak. Process commit was about
13.6 GB. These observations do not establish which command caused device loss.

## Measured GPU costs

One four-frame world capture reports about 285 ms per frame across the timed
submission parts, with about 275 ms inside named passes/dispatches:

| Work | GPU time per frame |
| --- | ---: |
| Compute 16x9x12, SPIR-V hash da15ef54d6d5127b | 88.8 ms |
| 427x240 colour pass, about 277 draws | 45.8 ms |
| Another 427x240 colour pass, about 15 draws | 31.9 ms |
| Compute 8x9x12, hash 346ba2552446ef7e, one grouped occurrence | 23.2 ms |
| Compute 64x64x1, hash 4c40650c1b5aa34f, one grouped occurrence | 11.3 ms |
| Shown 720p composition/HUD passes | Less than 1 ms each |

A pass's shader identifiers describe its first draw, not a measurement of that
individual shader's contribution across every draw in the pass. Three-dimensional
dispatch shape alone does not prove that the expensive compute shader is fog.
New compute-map and fog-recognition messages provide that correlation in the
next player run. No shaders are blindly skipped based on this timing table.

## Compatibility geometry recovery

The old log's "shader failed to translate" mesh counts include a geometry-buffer
allocation failure. The renderer now separates translation, invalid compatibility
metadata, geometry allocation, and compute-pipeline failures. Successful lowered
mesh draws also remain classified as mesh draws in diagnostics.

Scratch capacity no longer exceeds the remaining groups in a direct/tail batch.
On a recoverable allocation failure, capacity is halved and retried down to one
parent group. All configured groups are still processed in successive batches;
this recovery alone does not remove geometry. Non-memory errors are not retried
as OOM, and device loss is reported immediately.

compat_mesh_reuse = auto retains one completed geometry buffer in low-memory
mode. On enables reuse independently; off restores the previous release policy.
The cache retains at most one buffer within the existing 32 MiB per-buffer budget,
preferring the largest to serve subsequent smaller requests. Reuse happens only
after its completion callback, before a new fence owns it. A mutex protects the
single cached pointer, and shutdown releases it before allocator destruction.
Idle cache bytes are reported separately from live bytes. This can avoid repeated
large mapped allocations, while retaining some idle memory.

## Optional quality and driver choices

```ini
compat_mesh_groups = 64
fog_cull = 1
submit_batch = 1
```

The defaults remain 4096, 0 and 4 respectively. Restart after changing them.

Reduced compatibility geometry follows task order and can remove nearby or useful
objects. Values 256 and 64 cap work per direct/indirect mesh draw. The full value
4096 restores the old indirect ceiling and leaves original direct-draw counts
intact. It does not affect HUD draw resolution or native mesh rendering. The CPU
previously recorded 4,096 parent groups per indirect draw, often in eight-group
batches, even when actual counts were much smaller. The quality cap limits that
recording work but is not distance-based LOD or a selective grass switch. GPU-side
actual counts and original group IDs remain intact for the processed prefix.

Fog culling keeps the game's original depth-based rejection instead of the
runtime's temporal-quality fix that computes hidden froxels. It can save work in
recognized fog preparation and dependent passes, but can restore fog trails behind
moving objects. It does not remove all fog or guarantee a gain in the 88.8 ms pass.
The recognizer logs the matched shader hash and policy when diagnostics are on.
Set fog_cull = 0 to restore the existing quality path.

One queue submission per call is the existing alternative to batching up to four.
It can improve latency or help isolate driver batching interactions, at the cost
of more CPU driver calls. No GPU waits or lower frame limit are introduced. It is
not a demonstrated cure for the latest device loss.

## Further diagnostic help

- Compute-map messages join GPU SPIR-V hashes to cache-key prefixes, program IDs,
  local workgroup sizes and declared image resource types/formats, once per variant.
- World-size observation now accepts a matching single light/depth pair as well
  as a G-buffer, so forward-rendered scenes can appear in the overlay.
- In timed mode, first observation requests the next available four-frame GPU
  capture window. Existing in-flight queries are not reset to start it early.
- Queue history records submit attempts/failures. Fatal snapshots show logical
  issued, handed-to-queue and completed-side-effect values without GPU queries.
- Completed geometry callbacks can report when the last draw's actual group count
  exceeded the reduced cap. That is not an exhaustive record of every capped draw.

These changes were compiler/content/package checked. The coding agent did not run
the game, unit tests or synthetic cases. The next player run must evaluate visual
completeness, performance and crash behavior. Restore full geometry, fog quality,
submission batching and standard memory policies independently as needed.
