/* Toggle GPIO1_C0 (GPIO line 48, JP4.31) as an execution indicator. */
#include <stdint.h>

#define GPIO1_BASE 0xFF870000u
#define GPIO1_SWPORT_DR_H  (*(volatile uint32_t *)(GPIO1_BASE + 0x04u))
#define GPIO1_SWPORT_DDR_H (*(volatile uint32_t *)(GPIO1_BASE + 0x0Cu))
#define GPIO1_C0_WRITE_EN  (1u << 16)

#define DELAY_ITERS 0x04000000u

static void gpio1_c0_write(uint32_t high)
{
    /* RK3506 GPIO V2 uses hiword write masks for pins 16-31. */
    GPIO1_SWPORT_DR_H = GPIO1_C0_WRITE_EN | (high ? 1u : 0u);
}

void m0_main(void)
{
    GPIO1_SWPORT_DDR_H = GPIO1_C0_WRITE_EN | 1u;
    gpio1_c0_write(0);

    uint32_t level = 0;
    for (;;)
    {
        asm volatile(
            ".syntax unified    \n"
            ".thumb             \n"
            "movs r0, %0        \n"
            "1: subs r0, #1     \n"
            "bne 1b             \n"
            :
            : "r" (DELAY_ITERS)
            : "r0");
        level ^= 1u;
        gpio1_c0_write(level);
    }
}
