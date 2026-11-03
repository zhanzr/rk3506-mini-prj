/*
  m0_read.c - A7-side reader for the M0 benchmark results.

  Maps the DDR shared memory at 0x03C00000 through /dev/mem and polls
  the result block the M0 firmware writes:

    word[0] magic   'M0BM' (0x4D30424D) - set by the firmware at start
    word[1] status  1 = running, 2 = calibration done
    word[2] param   calibration loop iterations
    word[3] value   estimated M0 cycles for the loop (3 cycles/iter)

  usage: m0_read [--poll]
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#define SHM_PHYS 0x03C00000u
#define SHM_MAGIC 0x4D30424Du
#define ST_RUN 1u
#define ST_DONE 2u

int main(int argc, char **argv)
{
    int poll = (argc > 1 && !strcmp(argv[1], "--poll"));
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    volatile uint32_t *shm;
    int i;

    if (fd < 0)
    {
        perror("/dev/mem");
        return 1;
    }
    shm = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, SHM_PHYS);
    if (shm == MAP_FAILED)
    {
        perror("mmap shm");
        return 1;
    }

    if (shm[0] != SHM_MAGIC)
    {
        printf("shm[0]=0x%08x - no magic yet (M0 not started or DDR shmem unreachable)\n", shm[0]);
        return 1;
    }
    printf("magic OK  status=%u  param=%u\n", shm[1], shm[2]);

    while (poll && shm[1] != ST_DONE)
        usleep(100000);

    if (shm[1] == ST_DONE)
    {
        unsigned long long cycles = (unsigned long long)shm[3];
        double sec = 0.0;
        printf("DONE: cal loop %u iters = %llu cycles (3 cyc/iter)\n",
               shm[2], cycles);
        /* wall time is measured externally; rough M0 clock if ~5 s run */
        printf("enter measured elapsed seconds: ");
        if (scanf("%lf", &sec) == 1 && sec > 0)
        {
            double hz = cycles / sec;
            printf("M0 core clock ~ %.1f MHz\n", hz / 1e6);
        }
    }
    else
    {
        printf("status=RUN (still running) iterations not applicable\n");
    }

    close(fd);
    (void)i;
    return 0;
}
