#include <stdio.h>
#include "log.h"

int main(int argc, char **argv)
{
    log_init("t");
    if (log_config_from_argv(argc, argv) != 0)
        printf("config load failed\n");
    log_printf(LOG_LVL_INFO, "hello file test");
    log_printf(LOG_LVL_DEBUG, "debug line");
    return 0;
}
