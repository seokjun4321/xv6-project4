#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

void print_swapstat()
{
    int nr_read, nr_write;
    swapstat(&nr_read, &nr_write);
    printf("swapstat: NRR: %d, NRW: %d\n", nr_read, nr_write);
}

int main()
{
    printf("=== TEST START ===\n");

    sbrk(PGSIZE * 28000);
    print_swapstat();    // swapstat: NRR: 0, NRW: 0

    sbrk(PGSIZE * 2000);
    print_swapstat();    // swapstat: NRR: 0, NRW: 7504

    sbrk(PGSIZE * 7000); // out of memory
    print_swapstat();    // swapstat: NRR: 0, NRW: 28000

    exit(0);
}