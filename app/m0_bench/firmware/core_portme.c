/* RK3506 M0 port; the A7 measures the benchmark interval over GPIO1_C0. */
#include "coremark.h"
#include <stdint.h>

volatile struct m0_coremark_report m0_coremark_report;
ee_u32 default_num_contexts = 1;

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
    return (void *)0;
}

void
portable_free(void *p)
{
    (void)p;
}

CORETIMETYPE
barebones_clock(void)
{
    return 0;
}

#define GETMYTIME(_t)              (*_t = barebones_clock())
#define MYTIMEDIFF(fin, ini)       ((fin) - (ini))
static CORETIMETYPE start_time_val, stop_time_val;

void
start_time(void)
{
    GETMYTIME(&start_time_val);
    coremark_gpio_write(1);
}

void
stop_time(void)
{
    GETMYTIME(&stop_time_val);
    coremark_gpio_write(0);
}

CORE_TICKS
get_time(void)
{
    return MYTIMEDIFF(stop_time_val, start_time_val);
}

secs_ret
time_in_secs(CORE_TICKS ticks)
{
    (void)ticks;
    return 0;
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

int
ee_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
