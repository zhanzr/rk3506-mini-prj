/*
  blink_hello.c - RK3506-mini LED2 blinker + SoC temperature (Celsius).

  Blinks LED2 (/sys/class/leds/run) at 1 Hz. The console does NOT log
  the led on/off states - it prints only the SoC temperature in
  degrees Celsius (read from the TS-ADC thermal zone,
  /sys/class/thermal/thermal_zone0, type "soc-thermal"). The TS-ADC
  hardware supports up to 50 KS/s sampling, but through the kernel
  thermal framework the readable rate is ~1 Hz.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#define LED_DIR      "/sys/class/leds/run"
#define THERMAL_BASE "/sys/class/thermal"
#define BLINK_US     500000

static volatile sig_atomic_t g_run = 1;

static void on_signal(int sig)
{
	(void)sig;
	g_run = 0;
}

static int read_file(const char *path, char *buf, size_t size)
{
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, size - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return 0;
}

static int write_file(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);

	if (fd < 0)
		return -1;
	if (write(fd, value, strlen(value)) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/* first thermal zone with a readable temp, e.g. /sys/class/thermal/thermal_zone0 */
static int find_thermal(char *path, size_t size)
{
	DIR *d;
	struct dirent *e;
	char test[512];
	char buf[64];

	d = opendir(THERMAL_BASE);
	if (!d)
		return -1;
	while ((e = readdir(d)) != NULL) {
		if (strncmp(e->d_name, "thermal_zone", 12) != 0)
			continue;
		snprintf(test, sizeof(test), "%s/%s/temp",
			 THERMAL_BASE, e->d_name);
		if (read_file(test, buf, sizeof(buf)) == 0 && buf[0] != '\0') {
			snprintf(path, size, "%s", test);
			closedir(d);
			return 0;
		}
	}
	closedir(d);
	return -1;
}

static int led_brightness(int on)
{
	return write_file(LED_DIR "/brightness", on ? "1" : "0");
}

int main(void)
{
	char trig_save[128];
	char thermal[512];
	char tbuf[64];
	unsigned long n = 0;
	int have_thermal;

	printf("rk3506-mini: LED2 blink (no led-state logging) + SoC temp in C\n");

	if (read_file(LED_DIR "/trigger", trig_save, sizeof(trig_save)) != 0) {
		fprintf(stderr, "cannot read %s/trigger - LED not available\n",
			LED_DIR);
		return 1;
	}

	/* take the LED over from the heartbeat trigger */
	if (write_file(LED_DIR "/trigger", "none") != 0) {
		fprintf(stderr, "cannot set trigger to none\n");
		return 1;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	have_thermal = (find_thermal(thermal, sizeof(thermal)) == 0);

	while (g_run) {
		if (led_brightness(n & 1) != 0) {
			fprintf(stderr, "cannot write brightness\n");
			break;
		}
		n++;
		if (have_thermal) {
			int ok = (read_file(thermal, tbuf, sizeof(tbuf)) == 0);
			double c = ok ? strtod(tbuf, NULL) / 1000.0 : -99.0;

			printf("blink %lu: soc temp: %.1f C\n", n, c);
		} else {
			printf("blink %lu: soc temp: not exposed\n", n);
		}
		fflush(stdout);
		usleep(BLINK_US);
	}

	/* restore what the system had before we touched it */
	led_brightness(0);
	/* re-apply the saved trigger (e.g. "heartbeat") */
	{
		char *t = trig_save;
		/* trigger file lists all triggers, active one in [brackets] */
		char *b = strchr(t, '[');
		char *e = b ? strchr(b, ']') : NULL;

		if (b && e) {
			*e = '\0';
			write_file(LED_DIR "/trigger", b + 1);
			printf("restored trigger: %s\n", b + 1);
		}
	}
	printf("bye\n");
	return 0;
}
