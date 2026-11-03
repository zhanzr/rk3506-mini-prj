/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef COREMARK_CONFIG_H_
#define COREMARK_CONFIG_H_

#define COMPILER_FLAGS "-mcpu=cortex-m0 -mthumb -O2 -flto"

/* Configuration for ARM Cortex-M0 */
//#define COMPILER_FLAGS "-mcpu=cortex-m0"

#ifndef ITERATIONS
#define ITERATIONS 10000
#endif
/* Configuration for uncache */
//#define ITERATIONS 500

#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

#define COREMARK_CLOCKS_PER_SEC 1000

#endif /* COREMARK_CONFIG_H_ */
