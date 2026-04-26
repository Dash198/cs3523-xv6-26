#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Struct to track frames
struct frame_entry {
  int in_use;         // If current entry is being used.
  struct proc *owner; // Owner process.
  uint64 va;          // Virtual address
  uint64 pa;          // Physical address
  int is_swapping;    // if currently swapping
};

// Struct to track swap slots
struct swap_slot {
  int in_use;       // If current slot in swap is in use
  uint start_block; // Starting block number of the slot in disk
};

// Dedicated swap lock
struct spinlock swap_lock;

// metadata table for all swap slots.
struct swap_slot swap_table[MAX_SWAP_PAGES];

// Clock hand
int clock_hand = 0;

// Global frame table
struct frame_entry frame_table[MAX_USER_FRAMES];

// Swap logistics
int swap_bitmap[MAX_SWAP_PAGES];

// We make a lock for all three arrays
struct spinlock ft_lock;

// Initialize swap table
void swap_init(void) {
  initlock(&swap_lock, "swap_lock");
  for (int i = 0; i < MAX_SWAP_PAGES; i++) {
    swap_table[i].in_use = 0;
    swap_table[i].start_block = SWAP_START + 4 * i;
  }
}

// Go through the swap table to find a free swap slot
// Panic if none found.
uint get_free_swap_block(void) {
  struct swap_slot *s;

  acquire(&swap_lock);
  for (s = swap_table; s < &swap_table[MAX_SWAP_PAGES]; s++) {
    if (!s->in_use) {
      s->in_use = 1;
      release(&swap_lock);
      return s->start_block;
    }
  }

  release(&swap_lock);
  panic("Out of swap space");
}

// Free the given swap slot
void free_swap_block(uint block_number) {
  if (block_number < SWAP_START)
    return;

  uint idx = (block_number - SWAP_START) / 4;

  if (idx > MAX_SWAP_PAGES)
    panic("free_swap_block: out of bounds");

  acquire(&swap_lock);
  swap_table[idx].in_use = 0;
  release(&swap_lock);
}

void evict_page(void);

// Add a frame to the frame table (Self-Healing Version)
int add_frame(uint64 pa, uint64 va, struct proc *owner) {
  while (1) {
    acquire(&ft_lock);
    struct frame_entry *fe;

    // Attempt to claim a slot
    for (fe = frame_table; fe < &frame_table[MAX_USER_FRAMES]; fe++) {
      if (!fe->in_use) {
        fe->va = va;
        fe->pa = pa;
        fe->owner = owner;
        fe->in_use = 1;
        fe->is_swapping = 0;
        release(&ft_lock);
        return 0;
      }
    }
    release(&ft_lock);

    // This is a race condition where another process takes over the page slot
    // Self-heal and evict another page.
    evict_page();
  }
}

// Remove a frame from the frame table
void remove_frame(uint64 pa) {
  acquire(&ft_lock);
  struct frame_entry *fe;
  for (fe = frame_table; fe < &frame_table[MAX_USER_FRAMES]; fe++) {
    if (fe->in_use && !fe->is_swapping && fe->pa == pa) {
      fe->in_use = 0;
      release(&ft_lock);
      return;
    }
  }
  release(&ft_lock);
}

// Copy the data from a physical frame to swap
int swap_out(uint64 pa) {
  uint start_block = get_free_swap_block();
  for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
    struct buf *b = bread(1, start_block + i);
    memmove(b->data, (char *)pa + i * BSIZE, BSIZE);
    bwrite(b);
    brelse(b);
  }
  return start_block;
}

// Copy data from swap to a frame
void swap_in(uint64 pa, uint start_block) {
  for (int i = 0; i < BLOCKS_PER_PAGE; i++) {
    struct buf *b = bread(1, start_block + i);
    memmove((char *)pa + i * BSIZE, b->data, BSIZE);
    brelse(b);
  }
  free_swap_block(start_block);
}

// Select the frame to replace using
// the CLOCK algorithm
uint64 choose_victim(void) {
  // lock has been removed due to race conditions.

  // Store the best index, priority and no of frames scanned
  int best_idx = -1;
  int best_prio = -1;
  int frames_scanned = 0;

  // Keep looping till a victim is found or we fully scanned once
  for (;;) {
    struct frame_entry *frame = &frame_table[clock_hand];

    // If frame is not used or is swapping then skip
    if (frame->in_use && !frame->is_swapping) {
      pte_t *pte = walk(frame->owner->pagetable, frame->va, 0);

      // If PTE1 is 0, it means the pagetable is corrupted, skip
      if (pte) {

        // If access bit is 0, we found our victim
        if (!(*pte & PTE_A)) {
          if (best_idx == -1 || frame->owner->priority_level >= best_prio) {
            best_idx = clock_hand;
            best_prio = frame->owner->priority_level;

            // If least priority, then return immediately
            if (best_prio == 3) {
              break;
            }
          }
        } else {

          // Set it to 0
          *pte = *pte & ~PTE_A;
        }
      }
    }

    frames_scanned++;
    if (frames_scanned == MAX_USER_FRAMES) {
      // If we have a candidate, then break
      if (best_idx != -1) {
        break;
      }
      // Otherwise re-scan
      else {
        frames_scanned = 0;
      }
    }

    clock_hand = (clock_hand + 1) % MAX_USER_FRAMES;
  }

  // Set the clock hand to point after the victim and return
  clock_hand = (best_idx + 1) % MAX_USER_FRAMES;

  return frame_table[best_idx].pa;
}

// Counts how many frames are currently used by all processes combined
int count_used_frames(void) {
  int count = 0;
  acquire(&ft_lock);
  for (int i = 0; i < MAX_USER_FRAMES; i++) {
    if (frame_table[i].in_use) {
      count++;
    }
  }
  release(&ft_lock);
  return count;
}

// Evict a page from the page table
void evict_page(void) {
  acquire(&ft_lock);

  // Get the victim address
  uint64 pa = choose_victim();

  // Find the virtual address and owner process
  uint64 va = 0;
  struct proc *owner = 0;
  struct frame_entry *fe;
  for (fe = frame_table; fe < &frame_table[MAX_USER_FRAMES]; fe++) {
    if (fe->pa == pa) {
      va = fe->va;
      owner = fe->owner;
      fe->is_swapping = 1;
      break;
    }
  }
  release(&ft_lock);

  // Store the ownder PID in case the owner is killed
  int og_pid = owner->pid;

  // Find an empty slot in the swap space and send the data to swap
  int swap_idx = swap_out(pa);
  if (swap_idx == -1) {
    panic("Out of swap space");
  }
  fe->is_swapping = 0;

  // If owner is killed, free the block and return
  if (og_pid != owner->pid || owner->state == UNUSED) {
    free_swap_block(swap_idx);
    fe->in_use = 0;
    return;
  }

  // Find the PTE
  pte_t *pte = walk(owner->pagetable, va, 0);

  if (pte == 0) {
    free_swap_block(swap_idx);
    fe->in_use = 0;
    return;
  }

  *pte = PTE_FLAGS(*pte) |
         ((uint64)swap_idx << 10); // Store the swap index in the pte
  *pte = *pte & ~PTE_V;            // Declare the address invalid
  *pte = *pte | PTE_SWAP;          // Set the swap bit to true

  owner->pages_evicted++;
  owner->pages_swapped_out++;
  owner->resident_pages--;

  // Free the page at the address
  kfree((void *)pa);

  fe->in_use = 0;

  // Clear the TLB cache
  sfence_vma();
}

// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void) {
  kernel_pagetable = kvmmake();
  // initialize the ft lock
  initlock(&ft_lock, "frametable");
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;
    if ((*pte & PTE_V) == 0) {
      // If invalid but in swap, then clear the respective swap index.
      if (*pte & PTE_SWAP) {
        int swap_idx = *pte >> 10;

        free_swap_block(swap_idx);

        *pte = 0;
        continue;
      }
      continue;
    }
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
      // remove the address from the frame table as well.
      remove_frame(pa);
    }
    *pte = 0;
  }
}

// Allocate PTEs and physical memory to grow a process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE) {
    // If all frames used then first evict a page.
    while (count_used_frames() >= MAX_USER_FRAMES) {
      evict_page();
    }
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) !=
        0) {
      kfree(mem);

      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }

    add_frame((uint64)mem, a, myproc());
    myproc()->resident_pages++;
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz, struct proc *newp) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0)
      continue; // page table entry hasn't been allocated
    if ((*pte & PTE_V) == 0) {
      if (*pte & PTE_SWAP) {
        // Page is on disk, bring it from swap
        vmfault(old, i, 0);
        // Re-walk the newly update pte
        pte = walk(old, i, 0);
      } else {
        continue;
      }
    }

    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    // If the system is at the max-frame capacity, force an eviction.
    while (count_used_frames() >= MAX_USER_FRAMES) {
      evict_page();
    }

    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }

    // Assign ownership to the new process
    add_frame((uint64)mem, i, newp);
    newp->resident_pages++;
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    pte = walk(pagetable, va0, 0);
    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0)
      return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64 vmfault(pagetable_t pagetable, uint64 va, int read) {
  uint64 mem;
  struct proc *p = myproc();

  p->page_faults++;

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  pte_t *pte = walk(p->pagetable, va, 0);

  // Check if the page is in swap
  if (pte && ((*pte & PTE_V) == 0 && (*pte & PTE_SWAP) != 0)) {

    int og_pid = p->pid;

    // Evict a page if all frames used and keep trying to allocate memory
    while (count_used_frames() >= MAX_USER_FRAMES) {
      evict_page();
    }
    mem = (uint64)kalloc();

    // Fetch the page from swap
    int swap_idx = *pte >> 10;
    swap_in(mem, swap_idx);

    if (p->pid != og_pid || p->state == UNUSED) {
      kfree((void *)mem);
      return 0;
    }

    p->pages_swapped_in++;
    p->pages_swapped_out--;
    p->resident_pages++;

    // Set the respective bits
    *pte = *pte & ~PTE_SWAP;
    *pte = *pte | PTE_V;

    *pte = PA2PTE(mem) | PTE_FLAGS(*pte);

    add_frame(mem, va, p);
    return mem;
  }
  if (ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64)kalloc();
  // If no memory available then evict a page and retry
  while (mem == 0) {
    evict_page();
    mem = (uint64)kalloc();
  }
  memset((void *)mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }

  p->resident_pages++;

  add_frame(mem, va, myproc());

  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V) {
    return 1;
  }
  return 0;
}
