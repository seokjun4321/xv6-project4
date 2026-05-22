#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NUMFORK 1024
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

    int pids[NUMFORK] = {0};
    sbrk(PGSIZE * 500);

    for(int i = 0; i < NUMFORK; i++) {
        pids[i] = fork();
        if(pids[i] == 0)
            exit(0);
    }
    print_swapstat(); // swapstat: NRR: 2004, NRW: 15716

    for(int i = 0; i < NUMFORK; i++)
        wait(0);

    exit(0);
}