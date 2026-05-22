#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int r1, w1, r2, w2;
  int npages = 200;

  if(argc > 1)
    npages = atoi(argv[1]);

  swapstat(&r1, &w1);
  printf("start: reads=%d writes=%d\n", r1, w1);

  char *base = sbrk(npages * 4096);
  if(base == (char*)-1){
    printf("sbrk failed\n");
    exit(1);
  }

  for(int i = 0; i < npages; i++){
    base[i * 4096] = (char)(i & 0xff);
  }

  swapstat(&r2, &w2);
  printf("after write: reads=%d writes=%d\n", r2, w2);

  int errors = 0;
  for(int i = 0; i < npages; i++){
    char expected = (char)(i & 0xff);
    if(base[i * 4096] != expected) errors++;
  }

  swapstat(&r2, &w2);
  printf("after read: reads=%d writes=%d errors=%d\n", r2, w2, errors);

  if(errors == 0) printf("PASS: data preserved across swap\n");
  else printf("FAIL: %d pages corrupted\n", errors);

  exit(0);
}
