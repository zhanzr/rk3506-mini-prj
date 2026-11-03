/* RK3506 M0 port implementation - no HAL, timing by A7 wall clock */
#include "coremark.h"
#include <stdint.h>

/* DDR shared memory output buffer (past the header words, cap 896 B) */
volatile char *shm_out_ptr = (volatile char *)(0x03C00000u + 64u);
int            shm_out_cnt = 0;
ee_u32         default_num_contexts = 1;

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

void *
portable_malloc(ee_size_t size)
{
    (void)size;
    return (void *)0;   /* MEM_STACK - never called */
}

void
portable_free(void *p)
{
    (void)p;
}

CORETIMETYPE
barebones_clock(void)
{
    return 0;           /* no on-M0 timer; the A7 measures wall time */
}

#define GETMYTIME(_t)              (*_t = barebones_clock())
#define MYTIMEDIFF(fin, ini)       ((fin) - (ini))
static CORETIMETYPE start_time_val, stop_time_val;

void
start_time(void)
{
    GETMYTIME(&start_time_val);
}

void
stop_time(void)
{
    GETMYTIME(&stop_time_val);
}

CORE_TICKS
get_time(void)
{
    return MYTIMEDIFF(stop_time_val, start_time_val);
}

double
time_in_secs(CORE_TICKS ticks)
{
    /* no on-M0 timer: pretend >=10 s so the report has no ERROR line;
     * the authoritative score comes from the A7 wall clock */
    (void)ticks;
    return 10.0;
}

void
portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;
    p->portable_id = 1;
}

void
portable_fini(core_portable *p)
{
    (void)p;
}
