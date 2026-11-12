// Bounded CPU-side graphics history. It describes recorded work, not GPU execution or completion.
#pragma once
#include "vk.h"

int vk_diag_enabled(void);
int vk_diag_timing(void);   // timed mode also enables the existing per-pass GPU timestamp profiler
uint64_t vk_diag_shader(const GfxShader *shader);   // first 16 hexadecimal digits of its cache key
void vk_diag_note(const char *operation, uint64_t fence, VkCommandBuffer cb,
                  uint64_t a, uint64_t b, uint64_t c, uint64_t d);
void vk_diag_mesh(uint64_t batches, uint64_t upper_groups, uint64_t ns);
void vk_diag_frame(uint64_t frame, uint64_t draws, uint64_t dispatches, uint64_t passes);
void vk_diag_report(void);   // no Vulkan calls, GPU waits or renderer locks; safe to call from a fatal path
