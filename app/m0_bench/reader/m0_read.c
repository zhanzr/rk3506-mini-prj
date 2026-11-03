#include <stdio.h>

int main(void)
{
    fputs(
        "The 0x03c00000 DDR polling protocol is not valid on this board image. "
        "Use board_coremark.py, which reads the M0 GPIO report.\n",
        stderr);
    return 2;
}
