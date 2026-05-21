// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

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
struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;
struct spinlock lru_lock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&lru_lock, "lru");
  freerange(end, (void*)PHYSTOP);
  page_lru_head = 0;
  num_lru_pages = 0;
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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
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