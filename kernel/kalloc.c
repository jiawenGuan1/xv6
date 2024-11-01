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

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

char lock_name[NCPU][24];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    snprintf(lock_name[i], sizeof(lock_name[i]), "kmem%d", i);
    initlock(&kmems[i].lock, lock_name[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
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

  // 获取当前CPU编号
  push_off();
  int cpu_id = cpuid();
  pop_off();

  // 将空闲页插入到对应 CPU 的空闲列表中
  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 获取当前 CPU 编号
  push_off();
  int cpu_id = cpuid();
  pop_off();

  // 尝试从当前 CPU 的空闲列表中分配内存块
  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;

  if (r) {  // 当前 CPU 有空闲块
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
  } else {  // 当前 CPU 无空闲块，尝试从其他 CPU 的列表中获取
    release(&kmems[cpu_id].lock);
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu_id) continue;

      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if (r) {  // 找到空闲块，更新链表并释放锁
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;
      } else {
        release(&kmems[i].lock);
      }
      // 如果所有 CPU 都无空闲块，返回 0
      if (i == NCPU - 1) return 0;
    }  
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
