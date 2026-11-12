// Additional opt-in shader compatibility checks. Native shaders are never rewritten here.
#pragma once
#include <stdint.h>

// A stale translator must not send Int64 modules to a device running the paired-word address layer.
static inline int vk_compat_spirv32_safe(const uint32_t *w, uint64_t n) {
    if (!w || n < 5 || w[0] != 0x07230203u) return 0;
    for (uint64_t at = 5; at < n;) {
        uint32_t op = w[at] & 0xffffu, len = w[at] >> 16;
        if (!len || len > n - at) return 0;
        if ((op == 17 && len >= 2 && w[at + 1] == 11) ||
            (op == 21 && len >= 4 && w[at + 2] == 64)) return 0;
        at += len;
    }
    return 1;
}
