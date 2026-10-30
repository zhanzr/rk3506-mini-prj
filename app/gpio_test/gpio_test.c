/*
 * gpio_test - toggle JP4 GPIO pins one by one so a LED module (wired in
 * the LCM's place) can verify every pin.
 *
 * Also fixes the RK3506 RM_IO trap: JP4.19/21/23/24/15 are GPIO0 bank-0
 * pins ("RM_IO" pins). Their 4-bit IOMUX (grf_pmu @ 0xff910000) may not
 * default to GPIO (mux 0), in which case plain GPIO writes never reach
 * the pin - which is why the bit-bang LCM showed nothing. For the free
 * pins this program sets the IOMUX nibble back to 0 (GPIO) via /dev/mem
 * (same write the pinctrl driver would do: value in low halfword,
 * write-enable mask in high halfword), then blinks each pin.
 *
 * Usage:
 *   gpio_test            # blink the free JP4 pins, one by one, forever
 *   gpio_test --once     # one pass, then exit
 *   gpio_test --all      # include pins claimed by enabled peripherals
 *                        # (those will NOT toggle - muxed to CAN/UART/SAI)
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define GRF_PMU_BASE 0xff910000ull /* bank0 IOMUX rows + RMIO matrix */

#define BIT_SPACING_US 400000 /* 1.25 Hz toggle - easy to probe */

struct pin {
    int jp4;
    int gpio;      /* linux gpio number */
    int bank, pin; /* SoC coordinates */
    const char *net;
    const char *owner; /* NULL = free in the stock image */
};

/* free in the stock image (mux forced to GPIO for bank0 RM_IO pins) */
static const struct pin pins_free[] = {
    { 23, 1, 0, 1, "SPI0_CLK", NULL },
    { 19, 3, 0, 3, "SPI0_MOSI", NULL },
    { 21, 6, 0, 6, "SPI0_MISO", NULL },
    { 24, 7, 0, 7, "SPI0_CSN0", NULL },
    { 15, 13, 0, 13, "GPIO0_B5 (LCM BLK)", NULL },
    { 29, 49, 1, 17, "GPIO1_C1 (OLED RES)", NULL },
    { 33, 47, 1, 15, "GPIO1_B7 (OLED DC)", NULL },
    { 31, 48, 1, 16, "GPIO1_C0", NULL },
    { 37, 45, 1, 13, "GPIO1_B5", NULL },
    { 35, 46, 1, 14, "GPIO1_B6", NULL },
};

/* claimed by enabled peripherals - toggling the gpio will not reach the
 * pin unless the peripheral is disabled in the DT */
static const struct pin pins_claimed[] = {
    { 7, 8, 0, 8, "GPIO0_B0", "SAI1_MCLK (codec)" },
    { 11, 9, 0, 9, "GPIO0_B1", "SAI1_SCLK (codec)" },
    { 13, 10, 0, 10, "GPIO0_B2", "SAI1_LRCK (codec)" },
    { 15, 13, 0, 13, "GPIO0_B5", "CAN1_TX" },
    { 18, 14, 0, 14, "GPIO0_B6", "UART3_TX" },
    { 22, 15, 0, 15, "GPIO0_B7", "UART3_RX" },
    { 8, 17, 0, 17, "GPIO0_C1", "UART1_TX" },
    { 10, 16, 0, 16, "GPIO0_C0", "UART1_RX" },
    { 26, 18, 0, 18, "GPIO0_C2", "CAN1_RX" },
    { 32, 19, 0, 19, "GPIO0_C3", "CAN0_TX" },
    { 36, 20, 0, 20, "GPIO0_C4", "CAN0_RX" },
    { 12, 2, 0, 2, "GPIO0_A2", "UART2_TX" },
    { 16, 0, 0, 0, "GPIO0_A0", "UART2_RX" },
};

static volatile uint32_t *grf_pmu;

/* set GPIO0 bank IOMUX nibble to mux 0 (GPIO). pins 0-7 -> reg 0x0,
 * 8-15 -> 0x8, 16-23 -> 0x10; 4 bits per pin; GRF write-enable is the
 * same bits in the high halfword */
static int rmio_pin_force_gpio(int pin)
{
    volatile uint32_t *reg = grf_pmu + (pin / 8) * 2; /* 0x0/0x8/0x10 */
    int shift = (pin % 8) * 4;
    uint32_t v;

    v = *reg;
    printf("    iomux@0x%03x pin %d: was 0x%x (nibble=0x%x)",
           (pin / 8) * 8, pin, v, (v >> shift) & 0xF);
    v = (v & ~(0xFu << shift)) & 0xFFFFu; /* clear nibble */
    *reg = v | (0xFu << (16 + shift));    /* + write-enable high word */
    v = *reg;
    printf(" -> now nibble=0x%x\n", (v >> shift) & 0xF);
    if (((v >> shift) & 0xF) != 0)
    {
        printf("    WARNING: nibble did not stick to 0\n");
        return -1;
    }
    return 0;
}

static int gpio_blink(int gpio, int seconds)
{
    char dev[32];
    int fd;
    struct gpio_v2_line_request req;

    snprintf(dev, sizeof(dev), "/dev/gpiochip%d", gpio / 32);
    fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        printf("    open %s: %s\n", dev, strerror(errno));
        return -1;
    }
    memset(&req, 0, sizeof(req));
    req.offsets[0] = gpio % 32;
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "gpio_test");
    if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
    {
        printf("    request: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    {
        struct gpio_v2_line_values vals;
        int on = 0;

        vals.mask = 1;
        for (int i = 0; i < seconds * 2; i++)
        {
            on = !on;
            vals.bits = on ? 1 : 0;
            ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
            usleep(BIT_SPACING_US);
        }
    }
    close(req.fd);
    close(fd);
    return 0;
}

static void test_set(const struct pin *pins, int n, int once)
{
    for (int i = 0;; i = (i + 1) % n)
    {
        const struct pin *p = &pins[i];

        printf("\n==> JP4.%d  gpio%-3d  %s", p->jp4, p->gpio, p->net);
        if (p->owner)
            printf("   [claimed by %s - expected NOT to toggle]", p->owner);
        printf("\n");

        /* bank0 pins 0..23 are RM_IO pins: force mux 0 (GPIO) unless the
         * pin is owned by an enabled peripheral */
        if (p->bank == 0 && p->pin < 24 && !p->owner)
            rmio_pin_force_gpio(p->pin);

        gpio_blink(p->gpio, 3);
        printf("    done\n");
        if (once && i == n - 1)
            break;
    }
}

int main(int argc, char **argv)
{
    int all = (argc > 1 && strcmp(argv[1], "--all") == 0);
    int once = (argc > 1 && strcmp(argv[1], "--once") == 0);
    int fd;

    setvbuf(stdout, NULL, _IONBF, 0);

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        perror("/dev/mem");
        return 1;
    }
    grf_pmu = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                   GRF_PMU_BASE & ~0xFFFull);
    if (grf_pmu == MAP_FAILED)
    {
        perror("mmap grf_pmu");
        return 1;
    }

    printf("gpio_test: JP4 pin walker (LED module in the LCM socket)\n");
    if (all)
        test_set(pins_claimed,
                 (int)(sizeof(pins_claimed) / sizeof(pins_claimed[0])),
                 once);
    test_set(pins_free, (int)(sizeof(pins_free) / sizeof(pins_free[0])),
             once);
    printf("\nall pins done\n");
    return 0;
}
