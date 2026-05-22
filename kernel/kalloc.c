// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// pa4: struct for page control
struct page pages[(PHYSTOP - KERNBASE) / PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;
struct spinlock lru_lock;

// pa4: swap space slot 관리용 비트맵
static char *swap_bitmap;
static struct spinlock swap_bitmap_lock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&lru_lock, "lru");
  initlock(&swap_bitmap_lock, "swap_bm");
  freerange(end, (void*)PHYSTOP);
  page_lru_head = 0;
  num_lru_pages = 0;

  // swap 비트맵을 위한 한 페이지 확보
  swap_bitmap = (char*)kalloc();
  if(swap_bitmap == 0)
    panic("kinit: swap_bitmap alloc");
  memset(swap_bitmap, 0, PGSIZE);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  num_free_pages++;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// pa4: kalloc function
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    num_free_pages--;
  }
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }

  // pa4: freelist가 비었으면 swap-out 시도
  char *pa = swap_out();
  if(pa == 0){
    extern int nr_sectors_read, nr_sectors_write;
    printf("kalloc: OOM (sw=%d sr=%d)\n", nr_sectors_write, nr_sectors_read);
    return 0;
  }
  memset(pa, 5, PGSIZE);
  return (void*)pa;
}

// pa4: 원형 doubly linked list의 tail에 추가
void
lru_add(char *pa, pagetable_t pt, uint64 va)
{
  struct page *p = PA2PAGE(pa);

  p->pagetable = pt;
  p->vaddr = (char*)va;

  acquire(&lru_lock);
  if(page_lru_head == 0){
    p->next = p;
    p->prev = p;
    page_lru_head = p;
  } else {
    struct page *tail = page_lru_head->prev;
    p->next = page_lru_head;
    p->prev = tail;
    tail->next = p;
    page_lru_head->prev = p;
  }
  num_lru_pages++;
  release(&lru_lock);
}

// pa4: called only when page is in LRU
void
lru_remove(char *pa)
{
  struct page *p = PA2PAGE(pa);

  acquire(&lru_lock);
  if(p->next == p){
    page_lru_head = 0;
  } else {
    p->prev->next = p->next;
    p->next->prev = p->prev;
    if(page_lru_head == p)
      page_lru_head = p->next;
  }
  p->next = 0;
  p->prev = 0;
  p->pagetable = 0;
  p->vaddr = 0;
  num_lru_pages--;
  release(&lru_lock);
}

// pa4: swap 공간에서 빈 slot 하나 할당. blkno 반환. 없으면 -1.
int
swap_alloc(void)
{
  acquire(&swap_bitmap_lock);
  for(int i = 0; i < SWAPMAX; i++){
    int byte = i / 8;
    int bit = i % 8;
    if((swap_bitmap[byte] & (1 << bit)) == 0){
      swap_bitmap[byte] |= (1 << bit);
      release(&swap_bitmap_lock);
      return i;
    }
  }
  release(&swap_bitmap_lock);
  return -1;
}

// pa4: blkno에 해당하는 swap slot 해제
void
swap_free(int blkno)
{
  if(blkno < 0 || blkno >= SWAPMAX)
    panic("swap_free: bad blkno");
  acquire(&swap_bitmap_lock);
  int byte = blkno / 8;
  int bit = blkno % 8;
  swap_bitmap[byte] &= ~(1 << bit);
  release(&swap_bitmap_lock);
}

// pa4: clock 알고리즘으로 victim 페이지를 LRU에서 골라 반환.
// LRU가 비어있으면 0.
static struct page *
reclaim(void)
{
  acquire(&lru_lock);
  if(page_lru_head == 0){
    release(&lru_lock);
    return 0;
  }

  for(;;){
    struct page *victim = page_lru_head;
    pte_t *pte = walk(victim->pagetable, (uint64)victim->vaddr, 0);

    if(pte == 0){
      // 비정상 — 그냥 리스트에서 떼고 다음 후보로 (방어용)
      if(victim->next == victim){
        page_lru_head = 0;
        release(&lru_lock);
        return 0;
      }
      victim->prev->next = victim->next;
      victim->next->prev = victim->prev;
      page_lru_head = victim->next;
      victim->next = 0;
      victim->prev = 0;
      num_lru_pages--;
      continue;
    }

    if(!(*pte & PTE_U)){
      // user 비트 없는 페이지는 swap 대상 아님 — 떼고 다음 후보로
      if(victim->next == victim){
        page_lru_head = 0;
        release(&lru_lock);
        return 0;
      }
      victim->prev->next = victim->next;
      victim->next->prev = victim->prev;
      page_lru_head = victim->next;
      victim->next = 0;
      victim->prev = 0;
      num_lru_pages--;
      continue;
    }

    if(*pte & PTE_A){
      // 최근에 접근됨 → 한 번 봐주고 head를 다음으로
      *pte &= ~PTE_A;
      page_lru_head = page_lru_head->next;
      continue;
    }

    // PTE_A == 0 → victim 확정. LRU에서 제거.
    if(victim->next == victim){
      page_lru_head = 0;
    } else {
      victim->prev->next = victim->next;
      victim->next->prev = victim->prev;
      page_lru_head = victim->next;
    }
    victim->next = 0;
    victim->prev = 0;
    num_lru_pages--;
    release(&lru_lock);
    return victim;
  }
}

// pa4: victim 페이지를 swap에 쓰고 PTE를 swap 형태로 바꾼 뒤,
// 빈 물리 페이지의 시작 주소를 반환. 실패 시 0.
char *
swap_out(void)
{
  static int so_count = 0;
  if(++so_count == 1 || so_count % 100 == 0)
    printf("[so:%d]", so_count);
  struct page *victim = reclaim();
  if(victim == 0)
    return 0;

  int blkno = swap_alloc();
  if(blkno < 0)
    return 0;

  uint64 pa = PAGE2PA(victim);
  pte_t *pte = walk(victim->pagetable, (uint64)victim->vaddr, 0);
  if(pte == 0){
    swap_free(blkno);
    return 0;
  }

  // swapwrite는 myproc()->pagetable로 user va를 해석하므로
  // victim의 pagetable로 잠깐 바꿔치기
  struct proc *p = myproc();
  pagetable_t saved = p->pagetable;
  p->pagetable = victim->pagetable;

  swapwrite((uint64)victim->vaddr, blkno);

  p->pagetable = saved;

  uint64 flags = PTE_FLAGS(*pte);
  *pte = ((uint64)blkno << 10) | (flags & ~(PTE_V | PTE_A));
  sfence_vma();

  return (char*)pa;
}

// pa4: swapped-out 페이지를 디스크에서 읽어 새 물리 페이지에 매핑.
// 성공 0, 실패 -1.
int
swap_in(pagetable_t pagetable, uint64 va)
{
  va = PGROUNDDOWN(va);
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0 || *pte == 0)
    return -1;
  if(*pte & PTE_V)
    return -1;

  int blkno = (*pte) >> 10;

  char *new_pa = kalloc();
  if(new_pa == 0)
    return -1;

  // PTE를 먼저 새 물리 페이지로 매핑 (swapread가 va 통해 쓰기 위함)
  // copyout이 PTE_W를 요구하므로 임시로 writable 설정
  uint64 saved_flags = PTE_FLAGS(*pte);
  *pte = PA2PTE(new_pa) | saved_flags | PTE_V | PTE_W;
  sfence_vma();

  swapread(va, blkno);

  // 원래 권한으로 복원
  *pte = PA2PTE(new_pa) | saved_flags | PTE_V;
  sfence_vma();

  swap_free(blkno);

  if(saved_flags & PTE_U)
    lru_add(new_pa, pagetable, va);

  return 0;
}