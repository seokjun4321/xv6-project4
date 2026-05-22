#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int npages = 200;
  if(argc > 1) npages = atoi(argv[1]);

  char *base = sbrk(npages * 4096);
  if(base == (char*)-1){
    printf("sbrk failed\n");
    exit(1);
  }

  // 1단계: write
  for(int i = 0; i < npages; i++){
    base[i * 4096] = (char)(i & 0xff);
  }
  printf("write done\n");

  // 2단계: read + verify
  int errors = 0;
  for(int i = 0; i < npages; i++){
    char expected = (char)(i & 0xff);
    if(base[i * 4096] != expected) errors++;
  }
  printf("read done, errors=%d\n", errors);

  if(errors == 0) printf("PASS\n");
  else printf("FAIL\n");

  // 끝에 swapstat 한 번 시도 (curiosity)
  int r, w;
  int ret = swapstat(&r, &w);
  printf("swapstat returned %d, reads=%d writes=%d\n", ret, r, w);

  exit(0);
}
