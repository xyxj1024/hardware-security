#include <stdio.h>
#include <stdint.h>

#define N 10

uint8_t array[N * 4096];

void clear_cache(void *start, void *end)
{
    const int syscall = 0xf002; // __ARM_NR_cacheflush
    asm volatile(
        "mov    r0, %0\n"
        "mov    r1, %1\n"
        "mov    r7, %2\n"
        "mov    r2, #0x0\n"
        "svc    0x00000000\n"
        :
        : "r"(start), "r"(end), "r"(syscall)
        : "r0", "r1", "r7");
}

static inline unsigned int get_ccnt(void)
{
    unsigned int value;
    asm volatile("MRC p15, 0, %0, c9, c13, 0\t\n"
                 : "=r"(value));
    return value;
}

static inline void init_pmcr(int32_t do_reset, int32_t enable_divider)
{
    // In general enable all counters (including cycle counter)
    int32_t value = 1;

    // Peform reset:
    if (do_reset)
    {
        value |= 2; // reset all counters to zero
        value |= 4; // reset cycle counter to zero
    }

    if (enable_divider)
        value |= 8; // enable "by 64" divider for CCNT

    value |= 16;

    // Set PMCR:
    asm volatile("MCR p15, 0, %0, c9, c12, 0\t\n" ::"r"(value));

    // Set CNTENS:
    asm volatile("MCR p15, 0, %0, c9, c12, 1\t\n" ::"r"(0x8000000f));

    // Clear overflows:
    asm volatile("MCR p15, 0, %0, c9, c12, 3\t\n" ::"r"(0x8000000f));
}

int main()
{
    uint64_t cc1, cc2, overhead;
    volatile uint8_t *addr;
    unsigned int junk = 0;
    int i;

    // Measure counting overhead:
    init_pmcr(1, 0);
    overhead = get_ccnt();
    overhead = get_ccnt() - overhead;

    for (i = 0; i < N; i++) {
        array[i * 4096] = 1;
    }

    for (i = 0; i < N; i++)
    {
        clear_cache(&array[i * 4096], &array[i * 4096] + 1);
    }

    // Wait for cache flushing to complete
    for (i = 0; i < 100; i++)
    {
    }

    array[3 * 4096] = 3;
    array[7 * 4096] = 7;

    for (i = 0; i < N; i++)
    {
        addr = &array[i * 4096];
        cc1 = get_ccnt();
        junk = *addr;
        cc2 = get_ccnt() - cc1;
        printf("Access time for array[%d * 4096]: %3u\n", i, (unsigned int)cc2);
    }
    printf("Overhead: %d\n", overhead);

    return 0;
}