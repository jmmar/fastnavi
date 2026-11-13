// The remaining explicit wait after lookup and look-ahead have used part of a draw's allowance.
#pragma once
#include <stdint.h>

static inline uint32_t vk_wait_remaining_ms(uint32_t allowance_ms, uint64_t elapsed_ns) {
    uint64_t allowance_ns = (uint64_t)allowance_ms * 1000000ull;
    return elapsed_ns < allowance_ns ? (uint32_t)((allowance_ns - elapsed_ns) / 1000000ull) : 0;
}
