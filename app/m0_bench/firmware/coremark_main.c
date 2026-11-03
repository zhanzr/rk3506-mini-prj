#include "coremark.h"
#include "coremark_report.h"

#include <stdint.h>

#define GPIO1_BASE 0xFF870000u
#define GPIO1_SWPORT_DR_H (*(volatile uint32_t *)(GPIO1_BASE + 0x04u))
#define GPIO1_SWPORT_DDR_H (*(volatile uint32_t *)(GPIO1_BASE + 0x0cu))
#define GPIO1_C0_WRITE_EN (1u << 16)

#ifndef COREMARK_GPIO_BIT_DELAY_LOOPS
#define COREMARK_GPIO_BIT_DELAY_LOOPS 2000000
#endif

#define REPORT_SYNC_UNITS 16u
#define REPORT_GAP_UNITS 8u
#define REPORT_VERSION 1u
#define REPORT_PAYLOAD_SIZE 19u

void coremark_gpio_write(int high)
{
    GPIO1_SWPORT_DR_H
        = GPIO1_C0_WRITE_EN | (high ? 1u : 0u);
}

static void delay_units(uint32_t units)
{
    while (units--)
    {
        __asm__ volatile(
            ".syntax unified\n"
            "ldr r0, =%c[count]\n"
            "1: subs r0, r0, #1\n"
            "bne 1b\n"
            :
            : [count] "i" (COREMARK_GPIO_BIT_DELAY_LOOPS)
            : "r0");
    }
}

static void send_byte(uint8_t byte)
{
    unsigned int bit;

    coremark_gpio_write(1);
    delay_units(1);
    for (bit = 0; bit < 8; ++bit)
    {
        coremark_gpio_write((byte >> bit) & 1u);
        delay_units(1);
    }
    coremark_gpio_write(0);
    delay_units(1);
}

static uint8_t crc8(const uint8_t *data, unsigned int length)
{
    uint8_t crc = 0;
    unsigned int i;

    for (i = 0; i < length; ++i)
    {
        unsigned int bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                : (uint8_t)(crc << 1);
    }
    return crc;
}

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void send_report(void)
{
    uint8_t payload[REPORT_PAYLOAD_SIZE];
    unsigned int offset = 0;

    payload[offset++] = 'M';
    payload[offset++] = '0';
    payload[offset++] = REPORT_VERSION;
    payload[offset++] = (uint8_t)m0_coremark_report.valid;
    put_u32_le(payload + offset, m0_coremark_report.iterations);
    offset += 4;
    put_u16_le(payload + offset, m0_coremark_report.seed_crc);
    offset += 2;
    put_u16_le(payload + offset, m0_coremark_report.list_crc);
    offset += 2;
    put_u16_le(payload + offset, m0_coremark_report.matrix_crc);
    offset += 2;
    put_u16_le(payload + offset, m0_coremark_report.state_crc);
    offset += 2;
    put_u16_le(payload + offset, m0_coremark_report.final_crc);
    offset += 2;
    payload[offset] = crc8(payload, offset);

    coremark_gpio_write(0);
    delay_units(REPORT_GAP_UNITS);
    coremark_gpio_write(1);
    delay_units(REPORT_SYNC_UNITS);
    coremark_gpio_write(0);
    delay_units(REPORT_GAP_UNITS);

    for (offset = 0; offset < sizeof(payload); ++offset)
        send_byte(payload[offset]);
}

void m0_main(void)
{
    GPIO1_SWPORT_DDR_H = GPIO1_C0_WRITE_EN | 1u;
    coremark_gpio_write(0);

    (void)coremark_main();
    send_report();

    for (;;)
        __asm__ volatile("wfi");
}
