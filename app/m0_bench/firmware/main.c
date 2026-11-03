/*
  main.c - RK3506 Cortex-M0: GPIO1_C0 (JP4.31) toggle liveness test.

  The M0 drives GPIO1_C0 (bus-domain GPIO1 @ 0xFF870000, pin 16) at
  ~1 Hz. The A7 polls the same pin through gpiod/sysfs and sees the
  alternation - decisive proof of M0 execution, independent of the
  mailbox/DDR channels.
*/
#include <stdint.h>

#define GPIO1_BASE 0xFF870000u
#define GPIO1_DR_H  (*(volatile uint32_t *)(GPIO1_BASE + 0x04u))  /* pins 16-31 data */
#define GPIO1_DDR_H (*(volatile uint32_t *)(GPIO1_BASE + 0x0Cu))  /* pins 16-31 dir */

#define CAL_ITERS  0x04000000u       /* ~1.4 s at ~140 MHz */

extern uint32_t _stack_top;
void reset_handler(void);

__attribute__((section(".vectors"), used))
const void *vectors[16] = {
    (const void *)0x00008000u,
    reset_handler,
    [2 ... 15] = (const void *)0
};

void m0_main(void)
{
    /* announce via the mailbox (best effort) */
    *(volatile uint32_t *)0xFF29001Cu = 0x40000000u;

    /* GPIO1_C0 output */
    GPIO1_DDR_H |= (1u << 0);

    /* toggle forever; the exact-cycle loops make each half-period
     * ~1.4 s (3 cycles per iteration on the M0) */
    for (;;)
    {
        asm volatile(
            ".syntax unified    \n"
            ".thumb             \n"
            "movs r0, %0        \n"
    "1: subs r0, #1       \n"
        "bne 1b             \n"
            :
            : "r" (CAL_ITERS)
            : "r0");
        GPIO1_DR_H ^= (1u << 0);
    }
}

void __attribute__((naked)) reset_handler(void)
{
    asm volatile(
        "cpsid i            \n"
        "bl m0_main         \n"
    "1: b 1b                \n");
}
