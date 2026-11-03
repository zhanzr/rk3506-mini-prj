/* RK3506 M0 port header for CoreMark - our own (overrides barebones) */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>
#include "coremark_config.h"

#define HAS_FLOAT 1
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 0
#define HAS_PRINTF 0

typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed int     ee_s32;
typedef float          ee_f32;  /* M0: no FPU, soft-float */
typedef unsigned char  ee_u8;
typedef unsigned int   ee_u32;
typedef ee_u32         ee_ptr_int;
typedef size_t         ee_size_t;
#define NULL ((void *)0)
#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD  MEM_STACK
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0

#define MAIN_HAS_NOARGC   1
#define MAIN_HAS_NORETURN 0

/* timing is done by the A7 via wall clock around the whole run */
#define EE_TICKS_PER_SEC 1

#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC"__VERSION__
#endif
#endif
#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS "-mcpu=cortex-m0 -Os"
#endif
#ifndef MEM_LOCATION
#define MEM_LOCATION "STACK"
#endif

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S
{
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

int ee_printf(const char *fmt, ...);

#endif /* CORE_PORTME_H */
