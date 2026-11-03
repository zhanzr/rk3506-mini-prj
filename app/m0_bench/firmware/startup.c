#include <stdint.h>

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

void m0_main(void);
void reset_handler(void);

__attribute__((section(".vectors"), used))
const void *vectors[16] = {
    (const void *)0x00008000u,
    reset_handler,
    [2 ... 15] = (const void *)0
};

void reset_handler(void)
{
    uint32_t *source = &_sidata;
    uint32_t *destination;

    __asm__ volatile("cpsid i");

    for (destination = &_sdata; destination < &_edata; ++destination)
        *destination = *source++;

    for (destination = &_sbss; destination < &_ebss; ++destination)
        *destination = 0;

    m0_main();
    for (;;)
        ;
}
