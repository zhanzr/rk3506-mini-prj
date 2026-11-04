/*
  blink_hello.c - RK3506-mini SoC temperature monitor.

  Polls the TS-ADC temperature via the kernel thermal zone
  (/sys/class/thermal/thermal_zone0, type "soc-thermal") and prints it
  in degrees Celsius. No LED / GPIO / trigger handling.

  The TS-ADC hardware supports up to 50 KS/s sampling, but through the
  kernel thermal framework the readable rate is ~1 Hz (the driver runs
  the sensor in automatic sweep mode and the framework caches the
  value). Datasheet: -40..125 C range, +/-5 C accuracy; the DT sets a
  120 C hardware shutdown.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#define THERMAL_BASE "/sys/class/thermal"
#define POLL_SEC     1

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

int main(void)
{
	char thermal[512];
	char tbuf[64];
	int have_thermal;

	printf("rk3506-mini SoC temperature (TS-ADC via %s)\n", THERMAL_BASE);

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	have_thermal = (find_thermal(thermal, sizeof(thermal)) == 0);

	while (g_run) {
		if (have_thermal) {
			double celsius;
			int ok = (read_file(thermal, tbuf, sizeof(tbuf)) == 0);

			if (ok) {
				celsius = strtod(tbuf, NULL) / 1000.0;
				printf("soc temp: %.1f C\n", celsius);
			} else {
				printf("soc temp: read failed\n");
			}
		} else {
			printf("soc temp: not exposed\n");
		}
		fflush(stdout);
		sleep(POLL_SEC);
	}

	printf("bye\n");
	return 0;
}
