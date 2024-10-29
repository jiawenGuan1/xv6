// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

char bcache_name[NBUCKETS][24];

struct {
  struct spinlock overall_lock;   //全局锁
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.overall_lock, "bcache");

  // 初始化哈希锁
  for (int i = 0; i < NBUCKETS; i++) {
    snprintf(bcache_name[i], sizeof(bcache_name[i]), "bcache%d", i);
    initlock(&bcache.lock[i], bcache_name[i]);

    // Create linked list of buffers
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  for (int i = 0; i < NBUF; i++) {
    uint hash_count = i % NBUCKETS;
    b = &bcache.buf[i];
    b->next = bcache.hashbucket[hash_count].next;
    b->prev = &bcache.hashbucket[hash_count];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[hash_count].next->prev = b;
    bcache.hashbucket[hash_count].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint hash_count = blockno % NBUCKETS;
  acquire(&bcache.lock[hash_count]);

  // 查找缓存中是否已经有指定磁盘块（dev 和 blockno），若找到匹配的缓冲区，则增加引用计数 refcnt 并返回该缓冲区
  for(b = bcache.hashbucket[hash_count].next; b != &bcache.hashbucket[hash_count]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hash_count]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果没找到，在自己的哈希桶通过 LRU 算法回收最近最久未使用且未被引用的缓冲区，复用该缓冲区来缓存新的磁盘块。
  for(b = bcache.hashbucket[hash_count].prev; b != &bcache.hashbucket[hash_count]; b = b->prev){
    if(b->refcnt == 0) {    // 没有进程正在使用,则回收该缓冲区来存储新的磁盘块
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[hash_count]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 本桶没有refcnt == 0的桶，就要找别的桶，但是需要先释放本桶的锁
  release(&bcache.lock[hash_count]);

  // 首先得获取全局锁，避免死锁的出现，且仅在挪用不同哈希桶之间的内存块时使用到了全局锁，因此并不影响其余进程的并发执行
  acquire(&bcache.overall_lock);
  acquire(&bcache.lock[hash_count]);

  // 遍历所有的哈希桶，寻找可以使用的块
  for (int i = 0; i < NBUCKETS; i++) {
    if(i == hash_count) continue;
    acquire(&bcache.lock[i]);   // 获取当前哈希桶的锁

    for (b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev) {
      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        // 把该块从哈希桶i中删除
        b->next->prev = b->prev;
        b->prev->next = b->next;

        // 将该块添加到当前哈希桶中
        b->next = &bcache.hashbucket[hash_count];
        b->prev = bcache.hashbucket[hash_count].prev;
        bcache.hashbucket[hash_count].prev->next = b;
        bcache.hashbucket[hash_count].prev = b;
        release(&bcache.lock[hash_count]);

        release(&bcache.lock[i]);
        release(&bcache.overall_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[i]);
  }
  release(&bcache.overall_lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint hash_count = b->blockno % NBUCKETS;
  acquire(&bcache.lock[hash_count]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 把该块从当前哈希桶中删除
    b->next->prev = b->prev;
    b->prev->next = b->next;

    // 将缓冲区 b 插入到最近使用的缓冲区链表头部，标记为最近使用的缓冲区
    b->next = bcache.hashbucket[hash_count].next;
    b->prev = &bcache.hashbucket[hash_count];
    bcache.hashbucket[hash_count].next->prev = b;
    bcache.hashbucket[hash_count].next = b;
  }
  
  release(&bcache.lock[hash_count]);
}

void
bpin(struct buf *b) {
  uint hash_count = b->blockno % NBUCKETS;
  acquire(&bcache.lock[hash_count]);
  b->refcnt++;
  release(&bcache.lock[hash_count]);
}

void
bunpin(struct buf *b) {
  uint hash_count = b->blockno % NBUCKETS;
  acquire(&bcache.lock[hash_count]);
  b->refcnt--;
  release(&bcache.lock[hash_count]);
}


