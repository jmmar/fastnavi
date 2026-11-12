# Optional low-memory profile

Set memory_profile = low in nx2navi.ini and restart after installing the updated
runtime. The Screen tab exposes the same choice under Performance and diagnostics.
The default is standard, which preserves the previous allocator/cache policies.
Select standard and restart to restore them on a more powerful PC. Existing
explicit allocator/cache settings still take precedence.

This pass was checked by compiler and packaging builds, not game execution or
unit tests. The player's latest pre-change run still ended in device loss after
using standard mesh batching, no speculative headroom and low textures. This
profile is a memory-pressure mitigation, not a demonstrated fix for that fault.

## Policies

| Resource or policy | Standard | Low memory |
| --- | --- | --- |
| General VMA block size on large heaps, when not overridden | 64 MiB | 16 MiB |
| Speculative next-block allocation, when not overridden | Enabled | Disabled |
| Dedicated staging block size | 64 MiB | 16 MiB |
| Minimum dedicated staging blocks retained | 2 | 0 |
| Completed standalone staging-buffer cache | Up to 192 MiB / 24 buffers | Up to 16 MiB / 24 buffers |
| Completed arena chunks retained | Up to 8 x 8 MiB | Up to 2 x 8 MiB |
| Released-texture cache when not overridden | Adaptive, 512 MiB to 2 GiB | 128 MiB |
| Process-commit pressure trigger for released textures | Installed RAM, at least 24 GiB | Two thirds of installed RAM, clamped to 8–12 GiB |
| Upload submission threshold | Existing submission points | Also at completed outer upload operations owning at least 32 MiB |

The upload threshold is soft. A single large operation can exceed it, and uploads
still in flight retain their resources until completion. It does not cap total
application or GPU memory. Submissions at the additional boundaries do not add a
GPU wait or change the frame limit. Smaller caches/blocks can increase allocation
work and texture uploads; standard remains available for throughput.

The profile does not change resolution, pixel sampling, geometry output, game
assets, saves, input bindings or the guest application pool. Raising the guest
pool would not fix a Vulkan allocation failure and can increase memory demand.

## Safe staging recovery

In low mode only, a staging allocation reporting out-of-host/device-memory:

1. Retires already-signalled upload batches without waiting.
2. Frees only completed idle staging buffers and arena chunks.
3. Retries the allocation in the original pool.
4. If that still reports OOM and a dedicated staging pool was used, tries the
   default allocator pool once.

The current operation and unsignalled batches retain their resources. Recovery
does not submit an open operation or wait for the GPU. Other errors, including
device loss, are not treated as recoverable OOM. An unrecoverable failure still
ends the run with its error report rather than silently losing texture data.
Arena allocation can also fall back to a buffer sized for the actual request,
as before. A small request can require a new allocation block; its byte count
alone does not describe the memory shortfall.

Upload-fence results are now checked before resource recycling. Device loss
or another fence error is reported instead of being mistaken for a pending
batch, and a failed blocking wait cannot recycle unfinished resources.

## Diagnostics and overrides

graphics_diag = on retains bounded history and workload summaries; timed adds GPU
pass timing. Auto now enables CPU diagnostics with the low-memory profile too.
Crash reports include batch-owned staging capacities split into open, in-flight,
cached and idle-arena categories, their recorded peak, and compatibility geometry
live/peak bytes. The staging figures exclude caller-owned staging, driver memory
and allocator block slack. Their atomic snapshot makes no GPU calls or renderer
lock acquisitions; it is best effort while other threads may still progress.

vma_block_mb, vram_headroom, tex_released_mb and tex_proc_commit_mb explicitly set
in the file or environment retain precedence over profile defaults. Do not copy
the entire example over an existing settings file. Merge memory_profile = low
from nx2navi-low-memory.ini.example and keep the other preferences.
