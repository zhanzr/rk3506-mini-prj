/*
 * gpio_test - toggle JP4 GPIO pins one by one so a LED module (wired in
 * the LCM's place) can verify every signal.
 *
 * Default sequence: the LCM signals (SCLK, MOSI, RESET, DC, CS, CS-alt),
 * 4 s each, then an ALL-blink phase where all pins toggle together for
 * 4 s. Loops forever; --once exits after one pass; --all additionally
 * walks the remaining free and the peripheral-claimed pins.
 *
 * Also fixes the RK3506 RM_IO trap: bank0 pins are "RM_IO" pins whose
 * IOMUX (grf_pmu @ 0xff910000) must be 0 (GPIO) for gpio writes to reach
 * the pin. On this image they were already 0x0 (verified 2026-10-30);
 * the force-write is kept as a safety/idempotent step.
 *
 * Usage:
 *   gpio_test            # LCM order + all-blink, loop forever
 *   gpio_test --once     # one pass, then exit
 *   gpio_test --all      # also walk remaining free + claimed pins
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

#define PIN_SECONDS 4    /* per-pin blink time */
#define ALL_SECONDS 4    /* all-blink time at end of each loop */

struct pin {
    int jp4;
    int gpio;      /* linux gpio number */
    int bank, pin; /* SoC coordinates */
    const char *name;
    const char *owner; /* NULL = free in the stock image */
};

/* the LCM signals in test order (CS twice: both wiring variants) */
static const struct pin pins_lcm[] = {
    { 23, 1, 0, 1, "SCLK", NULL },
    { 19, 3, 0, 3, "MOSI", NULL },
    { 29, 49, 1, 17, "RESET", NULL },
    { 33, 47, 1, 15, "DC", NULL },
    { 31, 48, 1, 16, "CS (JP4.31 variant)", NULL },
    { 24, 7, 0, 7, "CS (JP4.24 variant / SPI0_CSN0)", NULL },
};

/* remaining free pins (--all) */
static const struct pin pins_free[] = {
    { 21, 6, 0, 6, "MISO (unused by write-only LCM)", NULL },
    { 15, 13, 0, 13, "GPIO0_B5 (LCM BLK)", NULL },
    { 37, 45, 1, 13, "GPIO1_B5", NULL },
    { 35, 46, 1, 14, "GPIO1_B6", NULL },
};

/* claimed by enabled peripherals - expected not to toggle */
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

struct handle {
    const struct pin *p;
    int fd, hfd;
    unsigned bit;
};

static int handle_open(struct handle *h)
{
    char dev[32];
    const struct pin *p = h->p;

    snprintf(dev, sizeof(dev), "/dev/gpiochip%d", p->gpio / 32);
    h->fd = open(dev, O_RDWR | O_CLOEXEC);
    if (h->fd < 0)
    {
        printf("    open %s: %s\n", dev, strerror(errno));
        return -1;
    }
    {
        struct gpio_v2_line_request req;

        memset(&req, 0, sizeof(req));
        req.offsets[0] = (unsigned int)(p->gpio % 32);
        req.num_lines = 1;
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        snprintf(req.consumer, sizeof(req.consumer), "gpio_test");
        if (ioctl(h->fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
        {
            printf("    request: %s\n", strerror(errno));
            close(h->fd);
            h->fd = -1;
            return -1;
        }
        h->hfd = req.fd;
        h->bit = 1u;
    }
    return 0;
}

static void handle_set(struct handle *h, int on)
{
    struct gpio_v2_line_values vals;

    vals.mask = h->bit;
    vals.bits = on ? h->bit : 0;
    ioctl(h->hfd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
}

static void handle_close(struct handle *h)
{
    if (h->hfd >= 0) close(h->hfd);
    if (h->fd >= 0) close(h->fd);
    h->hfd = h->fd = -1;
}

static void test_set(const struct pin *pins, int n, int do_all_blink)
{
    struct handle h[16];

    for (int i = 0; i < n; i++)
    {
        const struct pin *p = &pins[i];

        printf("\n==> %s  (JP4.%d  gpio%-3d)", p->name, p->jp4, p->gpio);
        if (p->owner)
            printf("   [claimed by %s - expected NOT to toggle]", p->owner);
        printf("\n");

        if (p->bank == 0 && p->pin < 24 && !p->owner)
            rmio_pin_force_gpio(p->pin);

        h[i].p = p;
        if (handle_open(&h[i]) != 0)
        {
            h[i].hfd = h[i].fd = -1;
            continue;
        }

        {
            int on = 0;

            for (int t = 0; t < PIN_SECONDS * 2; t++)
            {
                on = !on;
                handle_set(&h[i], on);
                usleep(400000);
            }
        }
        printf("    done\n");
    }

    if (do_all_blink)
    {
        printf("\n==> ALL-blink: all %d pins toggling together (%d s)\n",
               n, ALL_SECONDS);
        for (int t = 0; t < ALL_SECONDS * 2; t++)
        {
            int on = t & 1;

            for (int i = 0; i < n; i++)
                if (h[i].hfd >= 0)
                    handle_set(&h[i], on);
            usleep(400000);
        }
        printf("    all-blink done\n");
    }

    for (int i = 0; i < n; i++)
        if (h[i].fd >= 0)
            handle_close(&h[i]);
}

int main(int argc, char **argv)
{
    int all = (argc > 1 && strcmp(argv[1], "--all") == 0);
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

    printf("gpio_test: LCM signals (SCLK, MOSI, RESET, DC, CS x2), "
           "%d s per pin + all-blink\n", PIN_SECONDS);
    test_set(pins_lcm, (int)(sizeof(pins_lcm) / sizeof(pins_lcm[0])), 1);
    if (all)
    {
        test_set(pins_free,
                 (int)(sizeof(pins_free) / sizeof(pins_free[0])), 1);
        test_set(pins_claimed,
                 (int)(sizeof(pins_claimed) / sizeof(pins_claimed[0])), 0);
    }
    printf("\nall pins done\n");
    return 0;
}
