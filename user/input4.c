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

    sbrk(PGSIZE * 15000);
    print_swapstat();        // swapstat: NRR: 0, NRW: 0

    int pid = fork();
    if(pid == 0) {
        print_swapstat();    // swapstat: NRR: 0, NRW: 7544

        sbrk(PGSIZE * 7000); // out of memory
        print_swapstat();    // swapstat: NRR: 0, NRW: 28000
        exit(0);
    }
    wait(0);
    print_swapstat();        // swapstat: NRR: 0, NRW: 28000
    
    exit(0);
}
