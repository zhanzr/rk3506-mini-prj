/*
 * SSD1317 "MD066" 0.66" 64x48 mono OLED driver (I2C) for the PocketBeagle.
 * Driver logic ported from the AT32F425 reference (f425-start
 * bare/ssd1317_md066_64x48_iic, itself ported from the Zhongjingyuan vendor
 * example STM32F103RC_IIC); transport layer ported from the MP157 DK1
 * reference (Linux i2c-dev + GPIO char-device uAPI).
 *
 * Kept from the vendor logic (with the AT32 port's fixes):
 *   - Set_Pos() page mapping: page 0xB2+y, visible rows = RAM pages 2..7
 *   - column base x+0x20, without the vendor's stray "| 0x01" which shifted
 *     every write one column right
 *   - batched page writes: one I2C transaction per 64-byte page row
 */

#include <errno.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "oled.h"

#define MD066_CMD 0
#define MD066_DATA 1

/* module write address in 8-bit notation, as used by the AT32 reference */
#define OLED_ADDR_8BIT 0x78

static int g_i2c_fd = -1;

void oled_set_i2c_fd(int fd)
{
    g_i2c_fd = fd;
}

void oled_delay_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
    {
        if (!oled_running)
            return;
    }
}

void Write_IIC_Byte_Buf(uint8_t slave_addr, uint8_t *buf, uint16_t size)
{
    (void)slave_addr;
    if (g_i2c_fd < 0)
    {
        fprintf(stderr, "oled: i2c fd not set\n");
        return;
    }
    if (write(g_i2c_fd, buf, size) != size)
    {
        fprintf(stderr, "oled i2c write: %s\n", strerror(errno));
    }
}

void Write_IIC_Command(unsigned char IIC_Command)
{
    uint8_t buf[2] = {0x00, IIC_Command};
    Write_IIC_Byte_Buf(OLED_ADDR_8BIT, buf, 2);
}

void Write_IIC_Data(unsigned char IIC_Data)
{
    uint8_t buf[2] = {0x40, IIC_Data};
    Write_IIC_Byte_Buf(OLED_ADDR_8BIT, buf, 2);
}

void OLED_WR_Byte(unsigned dat, unsigned cmd)
{
    if (cmd)
    {
        Write_IIC_Data(dat);
    }
    else
    {
        Write_IIC_Command(dat);
    }
}

/*
 * Batched write of one 64-byte page row in a single I2C transaction
 * (control byte 0x40 + 64 data bytes).
 */
static void OLED_WritePage(unsigned char page, const unsigned char *data)
{
    uint8_t buf[1 + 64];
    buf[0] = 0x40; /* control byte: data stream follows */
    memcpy(&buf[1], data, 64);
    OLED_Set_Pos(0, page);
    Write_IIC_Byte_Buf(OLED_ADDR_8BIT, buf, sizeof(buf));
}

/* fill the whole visible 64x48 window with one RAM byte pattern */
void OLED_Fill(unsigned char data)
{
    unsigned char row[64];
    unsigned char page;

    memset(row, data, sizeof(row));
    for (page = 0; page < 6; page++)
    {
        OLED_WritePage(page, row);
    }
}

/* whole-display inversion (mono "negative") */
void OLED_Invert(unsigned char on)
{
    OLED_WR_Byte(on ? 0xA7 : 0xA6, MD066_CMD);
}

/* vendor Set_Pos page mapping kept (visible rows = RAM pages 2..7), with a
 * one-column correction on top: the intended window base is column x+0x20,
 * but the vendor's extra "| 0x01" on the low nibble shifted every write one
 * column to the right - the leftmost visible column (32) was never written
 * and showed random pixels */
void OLED_Set_Pos(unsigned char x, unsigned char y)
{
    unsigned char col = x + 0x20;
    OLED_WR_Byte(0xb2 + y, MD066_CMD);
    OLED_WR_Byte(((col >> 4) & 0x0f) | 0x10, MD066_CMD);
    OLED_WR_Byte(col & 0x0f, MD066_CMD);
}

void OLED_Display_On(void)
{
    OLED_WR_Byte(0X8D, MD066_CMD);  /* SET DCDC */
    OLED_WR_Byte(0X14, MD066_CMD);  /* DCDC ON */
    OLED_WR_Byte(0XAF, MD066_CMD);  /* DISPLAY ON */
}

void OLED_Display_Off(void)
{
    OLED_WR_Byte(0X8D, MD066_CMD);  /* SET DCDC */
    OLED_WR_Byte(0X10, MD066_CMD);  /* DCDC OFF */
    OLED_WR_Byte(0XAE, MD066_CMD);  /* DISPLAY OFF */
}

/* clear the visible window (batched: 6 transactions of 64 bytes) */
void OLED_Clear(void)
{
    OLED_Fill(0x00);
}

/* display one char at the specified position
 * x: 0-63, y: page 0-5
 * size: font 16 (8x16) / 12 (6x8) */
void OLED_ShowChar(unsigned char x, unsigned char y, unsigned char chr,
                   unsigned char Char_Size)
{
    unsigned char c = 0, i = 0;
    c = chr - ' '; /* ASCII offset into the font table */
    if (x > Max_Column - 1)
    {
        x = 0;
        y = y + 2;
    }
    if (Char_Size == 16)
    {
        OLED_Set_Pos(x, y);
        for (i = 0; i < 8; i++)
            OLED_WR_Byte(F8X16[c * 16 + i], MD066_DATA);
        OLED_Set_Pos(x, y + 1);
        for (i = 0; i < 8; i++)
            OLED_WR_Byte(F8X16[c * 16 + i + 8], MD066_DATA);
    }
    else
    {
        OLED_Set_Pos(x, y);
        for (i = 0; i < 6; i++)
            OLED_WR_Byte(F6x8[c][i], MD066_DATA);
    }
}

/* m^n helper */
static uint32_t oled_pow(unsigned char m, unsigned char n)
{
    uint32_t result = 1;
    while (n--)
        result *= m;
    return result;
}

/* display a number
 * x,y : start position
 * len : number of digits
 * size: font size
 * num: value (0~4294967295) */
void OLED_ShowNum(unsigned char x, unsigned char y, uint32_t num,
                  unsigned char len, unsigned char size2)
{
    unsigned char t, temp;
    unsigned char enshow = 0;
    for (t = 0; t < len; t++)
    {
        temp = (num / oled_pow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                OLED_ShowChar(x + (size2 / 2) * t, y, ' ', size2);
                continue;
            }
            else
                enshow = 1;
        }
        OLED_ShowChar(x + (size2 / 2) * t, y, temp + '0', size2);
    }
}

/* display a string */
void OLED_ShowString(unsigned char x, unsigned char y,
                     const unsigned char *chr, unsigned char Char_Size)
{
    unsigned char j = 0;
    while (chr[j] != '\0')
    {
        OLED_ShowChar(x, y, chr[j], Char_Size);
        x += 8;
        if (x > Max_Column - 8)
        {
            x = 0;
            y += 2;
        }
        j++;
    }
}

/* RES line control through the GPIO char-device request fd */
void oled_res_set(int res_fd, int on)
{
    struct gpio_v2_line_values vals;

    memset(&vals, 0, sizeof(vals));
    vals.mask = 1;
    vals.bits = on ? 1 : 0;
    if (ioctl(res_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0)
    {
        fprintf(stderr, "oled: RES set: %s\n", strerror(errno));
    }
}

void OLED_Init(int res_fd)
{
    /* RES pulse, vendor timing: high 100 ms, low 100 ms, high */
    oled_res_set(res_fd, 1);
    oled_delay_ms(100);
    oled_res_set(res_fd, 0);
    oled_delay_ms(100);
    oled_res_set(res_fd, 1);

    /* vendor init sequence for the SSD1317-based 64x48 module (verbatim) */
    OLED_WR_Byte(0xAE, MD066_CMD);  //--turn off oled panel

    OLED_WR_Byte(0x00, MD066_CMD);  //--set low column address 00
    OLED_WR_Byte(0x12, MD066_CMD);  //--set high column address 12
    OLED_WR_Byte(0x40, MD066_CMD);  //--set start line address
    OLED_WR_Byte(0x81, MD066_CMD);  //--set contrast control register
    OLED_WR_Byte(0xCF, MD066_CMD);  // Set SEG Output Current Brightness
    OLED_WR_Byte(0xA1, MD066_CMD);  //--Set SEG/Column Mapping (0xa0 normal, 0xa1 mirrored)
    OLED_WR_Byte(0xC8, MD066_CMD);  // Set COM/Row Scan Direction (0xc0 normal, 0xc8 mirrored)

    OLED_WR_Byte(0xA6, MD066_CMD);  //--set normal display

    OLED_WR_Byte(0xA8, MD066_CMD);  //--set multiplex ratio
    OLED_WR_Byte(0x3f, MD066_CMD);  //--1/64 duty

    OLED_WR_Byte(0xD3, MD066_CMD);  //-set display offset
    OLED_WR_Byte(0x00, MD066_CMD);  //-not offset

    OLED_WR_Byte(0xd5, MD066_CMD);  //--set display clock divide ratio/oscillator frequency
    OLED_WR_Byte(0x80, MD066_CMD);  //--100 frames/sec

    OLED_WR_Byte(0xD9, MD066_CMD);  //--set pre-charge period
    OLED_WR_Byte(0xF1, MD066_CMD);  // Pre-Charge 15 Clocks, Discharge 1 Clock
    OLED_WR_Byte(0xDA, MD066_CMD);  //--set com pins hardware configuration
    OLED_WR_Byte(0x12, MD066_CMD);

    OLED_WR_Byte(0xDB, MD066_CMD);  //--set vcomh
    OLED_WR_Byte(0x40, MD066_CMD);  // VCOM Deselect Level

    OLED_WR_Byte(0x20, MD066_CMD);  //-Set Page Addressing Mode
    OLED_WR_Byte(0x02, MD066_CMD);

    OLED_WR_Byte(0x8D, MD066_CMD);  //--set Charge Pump enable/disable
    OLED_WR_Byte(0x14, MD066_CMD);  //--charge pump enable

    OLED_WR_Byte(0xA4, MD066_CMD);  // Disable Entire Display On (0xa4/0xa5)
    OLED_WR_Byte(0xA6, MD066_CMD);  // Disable Inverse Display On (0xa6/a7)
    OLED_WR_Byte(0xAF, MD066_CMD);  //--turn on oled panel

    OLED_Clear();
    OLED_Set_Pos(0, 0);
}

/*
 * Full-screen mono test pattern set for the 64x48 window (from the AT32
 * port): banner/info -> inverted info -> all-on -> all-off -> 8-px
 * horizontal stripes -> 4-px horizontal stripes -> 1-px checkerboard ->
 * 8-px vertical bars -> border rectangle -> page-bit gradient -> animated
 * bar sweep with console FPS report
 */
static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void OLED_Demo(void)
{
    unsigned char row[64];
    unsigned char page, x;
    long t0, dt;
    uint32_t frames;
    char line[24];

    /* --- banner + info pages (8x16 font, max 8 chars/line) --- */
    OLED_Clear();
    OLED_ShowString(0, 0, (const unsigned char *)" SSD1317", 16);
    OLED_ShowString(0, 2, (const unsigned char *)" 64x48", 16);
    OLED_ShowString(0, 4, (const unsigned char *)" PB-A8", 16);
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- same info page, whole-display inverted --- */
    OLED_Invert(1);
    if (!oled_running)
        return;
    oled_delay_ms(1000);
    OLED_Invert(0);
    if (!oled_running)
        return;
    oled_delay_ms(500);

    /* --- all pixels on / off --- */
    OLED_Fill(0xFF);
    if (!oled_running)
        return;
    oled_delay_ms(1000);
    OLED_Fill(0x00);
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- 8-px horizontal stripes --- */
    for (page = 0; page < 6; page++)
    {
        memset(row, (page & 1) ? 0x00 : 0xFF, sizeof(row));
        OLED_WritePage(page, row);
    }
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- 4-px horizontal stripes --- */
    for (page = 0; page < 6; page++)
    {
        memset(row, (page & 1) ? 0xF0 : 0x0F, sizeof(row));
        OLED_WritePage(page, row);
    }
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- 1-px checkerboard --- */
    for (x = 0; x < 64; x++)
    {
        row[x] = (x & 1) ? 0xAA : 0x55;
    }
    for (page = 0; page < 6; page++)
    {
        OLED_WritePage(page, row);
    }
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- 8-px vertical bars --- */
    for (x = 0; x < 64; x++)
    {
        row[x] = ((x / 8) & 1) ? 0x00 : 0xFF;
    }
    for (page = 0; page < 6; page++)
    {
        OLED_WritePage(page, row);
    }
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- border rectangle (1-px frame) --- */
    for (page = 0; page < 6; page++)
    {
        memset(row, 0x00, sizeof(row));
        if (page == 0 || page == 5)
        {
            memset(row, 0xFF, sizeof(row));
        }
        else
        {
            row[0] = 0xFF;
            row[63] = 0xFF;
        }
        OLED_WritePage(page, row);
    }
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- page gradient (fake grey ramp on mono via row bits) --- */
    {
        static const unsigned char ramp[6] = {0x03, 0x0F, 0x3F,
                                              0xFF, 0x3F, 0x0F};
        for (page = 0; page < 6; page++)
        {
            memset(row, ramp[page], sizeof(row));
            OLED_WritePage(page, row);
        }
    }
    if (!oled_running)
        return;
    oled_delay_ms(1000);

    /* --- animated bar sweep with FPS on the console --- */
    OLED_Clear();
    t0 = now_ms();
    for (frames = 0; frames < 40 && oled_running; frames++)
    {
        unsigned char start = (frames * 4) % 64;
        memset(row, 0x00, sizeof(row));
        memset(&row[start], 0xFF, 8 <= 64 - start ? 8 : 64 - start);
        for (page = 0; page < 6; page++)
        {
            OLED_WritePage(page, row);
        }
    }
    dt = now_ms() - t0;
    if (dt < 1)
    {
        dt = 1;
    }
    snprintf(line, sizeof(line), "fps %lu",
             (unsigned long)(frames * 1000 / dt));
    OLED_ShowString(0, 2, (const unsigned char *)line, 16);
    printf("oled fps: %s\n", line);
    if (!oled_running)
        return;
    oled_delay_ms(1000);
}

/* vendor helper kept for parity with the reference projects */
void fill_picture(unsigned char fill_Data)
{
    unsigned char m, n;
    for (m = 0; m < 8; m++)
    {
        OLED_WR_Byte(0xb0 + m, 0);  /* page0-page7 */
        OLED_WR_Byte(0x00, 0);      /* low column start address */
        OLED_WR_Byte(0x10, 0);      /* high column start address */
        for (n = 0; n < 128; n++)
        {
            OLED_WR_Byte(fill_Data, 1);
        }
    }
}
