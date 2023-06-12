#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> /* for memset() */
#include <sched.h>  /* for CPU_SET, CPU_ZERO, sched_setaffinity() */
#include <unistd.h> /* for sysconf() */

#define CACHE_LINE_SIZE 64
#define DEFAULT_ALLOC_SIZE_KB 4096
#define DEFAULT_ITER 100

enum access_type
{
    READ,
    WRITE
};

int g_mem_size = DEFAULT_ALLOC_SIZE_KB * 1024;
int *g_mem_ptr = 0;
volatile uint64_t g_nread = 0;
volatile unsigned int g_start, g_end;

/* User-specified parameters: */
int acc_type = READ;
int cpuid = 0;

static inline unsigned int get_ccnt(void)
{
    unsigned int value;
    asm volatile("MRC p15, 0, %0, c9, c13, 0\t\n"
                 : "=r"(value));
    return value;
}

static inline void init_pmcr(int32_t do_reset, int32_t enable_divider)
{
    int32_t value = 1;

    if (do_reset)
    {
        value |= 2;
        value |= 4;
    }

    if (enable_divider)
        value |= 8;

    value |= 16;

    asm volatile("MCR p15, 0, %0, c9, c12, 0\t\n" ::"r"(value));

    asm volatile("MCR p15, 0, %0, c9, c12, 1\t\n" ::"r"(0x8000000f));

    asm volatile("MCR p15, 0, %0, c9, c12, 3\t\n" ::"r"(0x8000000f));
}

void set_cpu_affinity(int cpuid)
{
    cpu_set_t cmask;
    int nprocessors = sysconf(_SC_NPROCESSORS_CONF);
    CPU_ZERO(&cmask);
    CPU_SET(cpuid % nprocessors, &cmask);
    if (sched_setaffinity(0, nprocessors, &cmask) < 0)
    {
        perror("sched_setaffinity() error");
    }
    else
    {
        // fprintf(stderr, "Assigned to CPU%d\n", cpuid);
    }
}

void exit_fn(int param)
{
    double bandwidth;
    unsigned int elapsed = g_end - g_start;
    printf("Bytes read = %llu\n", (unsigned long long)g_nread);
    printf("CPU cycles = %u\n", elapsed);
    bandwidth = (double)g_nread / (double)elapsed;
    printf("CPU%d: Bandwidth = %lf bytes per cycle | ", cpuid, bandwidth);
    printf("CPU%d: Average = %lf cycles\n\n", cpuid, ((double)elapsed / ((double)g_nread / (double)CACHE_LINE_SIZE)));
    exit(0);
}

int64_t bench_read()
{
    int i;
    int64_t sum = 0;
    for (i = 0; i < g_mem_size / 4; i += (CACHE_LINE_SIZE / 4))
    {
        sum += g_mem_ptr[i];
    }
    g_nread += g_mem_size;
    return sum;
}

int bench_write()
{
    register int i;
    for (i = 0; i < g_mem_size / 4; i += (CACHE_LINE_SIZE / 4))
    {
        g_mem_ptr[i] = i;
    }
    g_nread += g_mem_size;
    return 1;
}

int main(int argc, char **argv)
{
    int64_t sum = 0;
    int i;

    if (argc > 1)
    {
        cpuid = atoi(argv[1]);
    }
    // printf("set_cpu_affinity...\n");
    set_cpu_affinity(cpuid);

    // printf("malloc...\n");
    g_mem_ptr = (int *)malloc(g_mem_size);
    memset((char *)g_mem_ptr, 1, g_mem_size);
    for (i = 0; i < g_mem_size / sizeof(int); i++)
    {
        g_mem_ptr[i] = i;
    }

    /* Actual memory access */
    // printf("Accessing memory...\n");
    init_pmcr(1, 0);
    g_start = get_ccnt();
    for (i = 0;; i++)
    {
        switch (acc_type)
        {
        case READ:
            // printf("Memory read...\n");
            sum += bench_read();
            break;
        case WRITE:
            // printf("Memory write...\n");
            sum += bench_write();
            break;
        }

        if (i >= DEFAULT_ITER)
        {
            break;
        }
    }
    g_end = get_ccnt();
    printf("Total sum  = %ld\n", (long)sum);
    exit_fn(0);

    return 0;
}