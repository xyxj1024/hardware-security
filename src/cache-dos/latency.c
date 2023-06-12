#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>  /* for CPU_SET, CPU_ZERO, sched_setaffinity() */
#include <unistd.h> /* for sysconf() */
#include "list.h"

#define CACHE_LINE_SIZE 64
#define DEFAULT_ALLOC_SIZE_KB 4096
#define DEFAULT_ITER 100

struct item
{
    int data;
    int in_use;
    struct list_head list;
} __attribute__((aligned(CACHE_LINE_SIZE)));
;

int g_mem_size = DEFAULT_ALLOC_SIZE_KB * 1024;

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

int main(int argc, char **argv)
{
    struct item *list;
    struct item *itmp;
    struct list_head head;
    struct list_head *pos;
    unsigned int start, end, ccdiff;
    int workingset_size = 1024;
    int i, j;
    int serial = 0;
    int tmp, next;
    int *perm;
    uint64_t readsum = 0;
    double avglat;

    if (argc > 1)
    {
        cpuid = atoi(argv[1]);
    }
    set_cpu_affinity(cpuid);

    workingset_size = g_mem_size / CACHE_LINE_SIZE;
    srand(0);
    INIT_LIST_HEAD(&head);

    /* Memory allocation */
    list = (struct item *)malloc(sizeof(struct item) * workingset_size + CACHE_LINE_SIZE);
    for (i = 0; i < workingset_size; i++)
    {
        list[i].data = i;
        list[i].in_use = 0;
        INIT_LIST_HEAD(&list[i].list);
    }

    /* Initialization */
    perm = (int *)malloc(workingset_size * sizeof(int));
    for (i = 0; i < workingset_size; i++)
    {
        perm[i] = i;
    }
    if (!serial)
    {
        for (i = 0; i < workingset_size; i++)
        {
            tmp = perm[i];
            next = rand() % workingset_size;
            perm[i] = perm[next];
            perm[next] = tmp;
        }
    }
    for (i = 0; i < workingset_size; i++)
    {
        list_add(&list[perm[i]].list, &head);
    }

    /* Actual memory access */
    init_pmcr(1, 0);
    start = get_ccnt();
    for (j = 0; j < DEFAULT_ITER; j++)
    {
        pos = (&head)->next;
        for (i = 0; i < workingset_size; i++)
        {
            itmp = list_entry(pos, struct item, list);
            readsum += itmp->data; /* read attack*/
            pos = pos->next;
        }
    }
    end = get_ccnt();

    ccdiff = end - start;
    avglat = (double)ccdiff / workingset_size / DEFAULT_ITER;
    printf("CPU cycles: %u (average: %lf)\n", ccdiff, avglat);
    printf("Bandwidth:  %lf bytes per cycle\n", (double)workingset_size / avglat);
    printf("Readsum:    %lld\n\n", (unsigned long long)readsum);
    return 0;
}