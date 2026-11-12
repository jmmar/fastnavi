// Small graphics history retained in memory and written only on a fatal error or device loss. Atomic fields allow a
// crash on the recorder/upload thread to take a best-effort snapshot while another thread is still recording.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "vk_diag.h"

#define HISTORY 64
typedef struct {
    uint32_t busy;
    uint64_t sequence, operation, frame, fence, cb, value[4];
} Entry;
static Entry history[HISTORY];
static uint64_t serial, current_frame, dropped;
static uint64_t mesh_batches, mesh_upper, mesh_ns;
static uint64_t mesh_live_bytes, mesh_peak_bytes;
static uint64_t mesh_idle_bytes;
static int mode, reported;

static int diag_mode(void) {
    int m = __atomic_load_n(&mode, __ATOMIC_ACQUIRE);
    if (m) return m;
    const char *s = cfg_value("NX2NAVI_GRAPHICS_DIAG");
    m = !strcmp(s, "timed") ? 3 : (!strcmp(s, "on") || !strcmp(s, "1") ||
        (!strcmp(s, "auto") && (cfg_bool("NX2NAVI_COMPAT_MESH") || cfg_bool("NX2NAVI_COMPAT_BDA32") || cfg_low_memory()))) ? 2 : 1;
    if (__atomic_compare_exchange_n(&mode, &(int){0}, m, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        if (m > 1) {
            __atomic_store_n(&rt_graphics_diagnostic, vk_diag_report, __ATOMIC_RELEASE);
            rt_log("graphics diag: bounded history and frame summaries on (ABI 1), GPU pass timing %s; graphics_diag=off disables",
                   m == 3 ? "on" : "off");
        }
    }
    return m;
}
int vk_diag_enabled(void) { return diag_mode() > 1; }
int vk_diag_timing(void) { return diag_mode() == 3; }
uint64_t vk_diag_shader(const GfxShader *shader) {
    if (!shader || !vk_diag_enabled()) return 0;
    const uint8_t *key = gfx_shader_key(shader);
    uint64_t prefix = 0;
    if (key) for (int i = 0; i < 8; i++) prefix = (prefix << 8) | key[i];
    return prefix;
}

// Replay thread only. Bridge the GPU profiler's SPIR-V hash to cache keys and declared image resources once per
// variant. This reads shader metadata, never game buffers or image contents.
void vk_diag_compute(const GfxShader *shader, uint64_t program, uint32_t x, uint32_t y, uint32_t z) {
    if (!shader || !vk_diag_enabled()) return;
    static uint64_t seen[192];
    static unsigned count;
    uint64_t key = vk_diag_shader(shader);
    for (unsigned i = 0; i < count; i++) if (seen[i] == key) return;
    if (count == 192) return;
    seen[count++] = key;
    uint64_t words = 0, hash = 0xcbf29ce484222325ull;
    const uint8_t *bytes = (const uint8_t *)gfx_shader_spirv(shader, &words);
    for (uint64_t i = 0; bytes && i < words * 4; i++) hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    uint32_t local[3];
    gfx_shader_local_size(shader, local);
    const GfxShaderRes *images;
    int n = gfx_shader_resources(shader, GFX_RES_IMG, &images);
    char resources[384] = "";
    size_t at = 0;
    for (int i = 0; i < n && i < 8 && at < sizeof resources - 50; i++)
        at += (size_t)snprintf(resources + at, sizeof resources - at, " image(slot=%d,type=%#x,fmt=%u)",
                               images[i].slot, images[i].type, images[i].format);
    rt_log("graphics diag: compute map program %llu hash %016llx key-prefix %016llx, local %ux%ux%u, "
           "first groups %ux%ux%u, %llu words, declared images %d:%s",
           (unsigned long long)program, (unsigned long long)hash, (unsigned long long)key, local[0], local[1], local[2],
           x, y, z, (unsigned long long)words, n, resources);
}

void vk_diag_note(const char *operation, uint64_t fence, VkCommandBuffer cb,
                  uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    if (!vk_diag_enabled()) return;
    uint64_t n = __atomic_add_fetch(&serial, 1, __ATOMIC_RELAXED);
    Entry *e = &history[n % HISTORY];
    if (__atomic_exchange_n(&e->busy, 1, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&dropped, 1, __ATOMIC_RELAXED);
        return;
    }
    __atomic_store_n(&e->sequence, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&e->operation, (uintptr_t)operation, __ATOMIC_RELAXED);   // callers pass string literals
    __atomic_store_n(&e->frame, __atomic_load_n(&current_frame, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    __atomic_store_n(&e->fence, fence, __ATOMIC_RELAXED);
    __atomic_store_n(&e->cb, (uintptr_t)cb, __ATOMIC_RELAXED);
    uint64_t value[4] = {a, b, c, d};
    for (int i = 0; i < 4; i++) __atomic_store_n(&e->value[i], value[i], __ATOMIC_RELAXED);
    __atomic_store_n(&e->sequence, n, __ATOMIC_RELEASE);
    __atomic_store_n(&e->busy, 0, __ATOMIC_RELEASE);
}

void vk_diag_mesh(uint64_t batches, uint64_t upper_groups, uint64_t ns) {
    if (!vk_diag_enabled()) return;
    __atomic_add_fetch(&mesh_batches, batches, __ATOMIC_RELAXED);
    __atomic_add_fetch(&mesh_upper, upper_groups, __ATOMIC_RELAXED);
    __atomic_add_fetch(&mesh_ns, ns, __ATOMIC_RELAXED);
}

void vk_diag_mesh_memory(int64_t bytes) {
    uint64_t live = bytes >= 0 ? __atomic_add_fetch(&mesh_live_bytes, (uint64_t)bytes, __ATOMIC_RELAXED)
                               : __atomic_sub_fetch(&mesh_live_bytes, (uint64_t)-bytes, __ATOMIC_RELAXED);
    uint64_t peak = __atomic_load_n(&mesh_peak_bytes, __ATOMIC_RELAXED);
    while (live > peak && !__atomic_compare_exchange_n(&mesh_peak_bytes, &peak, live, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
void vk_diag_mesh_idle(uint64_t bytes) { __atomic_store_n(&mesh_idle_bytes, bytes, __ATOMIC_RELAXED); }

void vk_diag_frame(uint64_t frame, uint64_t draws, uint64_t dispatches, uint64_t passes) {
    if (!vk_diag_enabled()) return;
    static uint64_t previous, last_log, frames, sum_draws, sum_dispatches, sum_passes;
    uint64_t now = plat_now_ns(), gap = previous ? now - previous : 0;
    previous = now;
    if (!last_log) last_log = now;
    frames++, sum_draws += draws, sum_dispatches += dispatches, sum_passes += passes;
    if (gap > 500000000ull || now - last_log >= 5000000000ull) {
        uint64_t batches = __atomic_exchange_n(&mesh_batches, 0, __ATOMIC_RELAXED);
        uint64_t upper = __atomic_exchange_n(&mesh_upper, 0, __ATOMIC_RELAXED);
        uint64_t cpu = __atomic_exchange_n(&mesh_ns, 0, __ATOMIC_RELAXED);
        rt_log("graphics diag: frame %llu gap %.1f ms; %llu frames / %.2f s: draws %llu, guest dispatches %llu, passes %llu; "
               "compat mesh batches %llu, group upper bounds %llu, recording/waits %.1f ms (not GPU time)",
               (unsigned long long)frame, gap / 1e6, (unsigned long long)frames, (now - last_log) / 1e9,
               (unsigned long long)sum_draws, (unsigned long long)sum_dispatches, (unsigned long long)sum_passes,
               (unsigned long long)batches, (unsigned long long)upper, cpu / 1e6);
        frames = sum_draws = sum_dispatches = sum_passes = 0, last_log = now;
    }
    __atomic_store_n(&current_frame, frame + 1, __ATOMIC_RELAXED);
}

void vk_diag_report(void) {
    if (!vk_diag_enabled() || __atomic_exchange_n(&reported, 1, __ATOMIC_RELAXED)) return;
    uint64_t end = __atomic_load_n(&serial, __ATOMIC_ACQUIRE);
    rt_log("graphics diag: recent CPU recording history (last 64); does not establish GPU progress/completion; %llu collision drops",
           (unsigned long long)__atomic_load_n(&dropped, __ATOMIC_RELAXED));
    rt_log("graphics diag: fill values = buffer/offset/bytes/pattern; mesh-fill = buffer/offset/bytes/allocation-bytes; "
           "guest-fill = buffer/offset/bytes/backed-end; "
           "mesh-batch = program/base/count/capacity; mesh-window = original-upper-bound/recorded-cap/first/indirect; "
           "pass = width/height/colour-format/depth-layer; "
           "dispatch = shader-key-prefix/groups-x/groups-y/groups-z; draw-ready = vertex-key/fragment-key/width/height; "
           "dispatch-indirect = shader-key-prefix/buffer/offset/unused; "
           "mesh-completed = buffer/allocation-bytes/last-count/last-base");
    uint64_t first = end >= HISTORY ? end - HISTORY + 1 : 1;
    for (uint64_t n = first; n <= end; n++) {
        Entry *e = &history[n % HISTORY];
        if (__atomic_load_n(&e->busy, __ATOMIC_ACQUIRE) || __atomic_load_n(&e->sequence, __ATOMIC_ACQUIRE) != n) continue;
        const char *op = (const char *)(uintptr_t)__atomic_load_n(&e->operation, __ATOMIC_RELAXED);
        uint64_t frame = __atomic_load_n(&e->frame, __ATOMIC_RELAXED), fence = __atomic_load_n(&e->fence, __ATOMIC_RELAXED);
        uint64_t cb = __atomic_load_n(&e->cb, __ATOMIC_RELAXED), v[4];
        for (int i = 0; i < 4; i++) v[i] = __atomic_load_n(&e->value[i], __ATOMIC_RELAXED);
        if (__atomic_load_n(&e->sequence, __ATOMIC_ACQUIRE) != n || __atomic_load_n(&e->busy, __ATOMIC_ACQUIRE)) continue;
        if (op && !strcmp(op, "pass")) {
            rt_log("graphics diag: #%llu frame %llu fence %llu cb %#llx pass %llux%llu colour-format %llu depth-layer %llu",
                   (unsigned long long)n, (unsigned long long)frame, (unsigned long long)fence, (unsigned long long)cb,
                   (unsigned long long)v[0], (unsigned long long)v[1], (unsigned long long)v[2], (unsigned long long)v[3]);
            continue;
        }
        rt_log("graphics diag: #%llu frame %llu fence %llu cb %#llx %s: %#llx %#llx %#llx %#llx",
               (unsigned long long)n, (unsigned long long)frame, (unsigned long long)fence, (unsigned long long)cb,
               op ? op : "unknown", (unsigned long long)v[0], (unsigned long long)v[1],
               (unsigned long long)v[2], (unsigned long long)v[3]);
    }
    double process, used, limit;
    plat_commit_info(&process, &used, &limit);
    rt_log("graphics diag: memory commit process %.0f MB, system %.0f / %.0f MB; "
           "unreported mesh batches %llu, upper groups %llu, CPU recording/waits %.1f ms", process, used, limit,
           (unsigned long long)__atomic_load_n(&mesh_batches, __ATOMIC_RELAXED),
           (unsigned long long)__atomic_load_n(&mesh_upper, __ATOMIC_RELAXED),
           __atomic_load_n(&mesh_ns, __ATOMIC_RELAXED) / 1e6);
    rt_log("graphics memory: compatibility geometry live %.1f MiB, peak %.1f MiB, completed idle %.1f MiB; memory profile %s",
           __atomic_load_n(&mesh_live_bytes, __ATOMIC_RELAXED) / 1048576.0,
           __atomic_load_n(&mesh_peak_bytes, __ATOMIC_RELAXED) / 1048576.0,
           __atomic_load_n(&mesh_idle_bytes, __ATOMIC_RELAXED) / 1048576.0,
           cfg_low_memory() ? "low" : "standard");
    vk_upload_diagnostic();
    vk_sync_diagnostic();
}
