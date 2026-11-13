#include <stdint.h>
#include <stdio.h>
#include "vk_wait_budget.h"

int main(void) {
    const struct { uint32_t allowance; uint64_t elapsed; uint32_t left; } cases[] = {
        {50, 0, 50}, {50, 20000000, 30}, {50, 49999999, 0},
        {50, 50000000, 0}, {50, 90000000, 0}, {0, 0, 0},
        {3, 1000000, 2}, {3, 3000000, 0}, {500, UINT64_MAX, 0},
        {UINT32_MAX, 1000000, UINT32_MAX - 1},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        uint32_t got = vk_wait_remaining_ms(cases[i].allowance, cases[i].elapsed);
        if (got != cases[i].left) {
            printf("wait budget case %u: got %u, expected %u\n", i, got, cases[i].left);
            return 1;
        }
        // Lookup/scan work plus the explicit wait cannot overspend the allowance unless that work already did.
        if (cases[i].elapsed <= (uint64_t)cases[i].allowance * 1000000ull &&
            cases[i].elapsed + (uint64_t)got * 1000000ull > (uint64_t)cases[i].allowance * 1000000ull)
            return 2;
    }
    puts("wait budget: elapsed lookup work, exhaustion and overflow checks passed");
    return 0;
}
