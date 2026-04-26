//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device
// virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "buf.h"
#include "virtio.h"

// Latency constant
#define DISK_C 5
// raid levels
#define RAID_0 0
#define RAID_1 1
#define RAID_5 5

// Current raid level
int raid_level = RAID_5;
// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// Linked list to queue buffers to each disk
struct {
  struct buf *head;   // Head
  struct buf *tail;   // Tail
  uint current_head;  // Current head position
  struct spinlock lock; // Dedicated lock
} disk_queue[4];

int disk_sched_policy = 0;  // 0 -> FCFS, 1 -> SSTF
int last_serviced_disk = 0; // Maintain a round robin to prevent starvation of disks
int disk_busy = 0;

static struct disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];

  struct spinlock vdisk_lock;

} disk;

void virtio_disk_init(void) {
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  // Initialise each disk queue
  for (int i = 0; i < 4; i++) {
    disk_queue[i].head = 0;
    initlock(&disk_queue[i].lock, "disk_queue");
  }

  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(VIRTIO_MMIO_VERSION) != 2 || *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
      *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    panic("could not find virtio disk");
  }

  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if (!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if (*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0)
    panic("virtio disk has no queue 0");
  if (max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if (!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for (int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int alloc_desc() {
  for (int i = 0; i < NUM; i++) {
    if (disk.free[i]) {
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void free_desc(int i) {
  if (i >= NUM)
    panic("free_desc 1");
  if (disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void free_chain(int i) {
  while (1) {
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if (flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int alloc3_desc(int *idx) {
  for (int i = 0; i < 3; i++) {
    idx[i] = alloc_desc();
    if (idx[i] < 0) {
      for (int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

// Select a buffer from a queue using FCFS, priority-aware
struct buf *select_fcfs(int disk) {
  struct buf *curr = disk_queue[disk].head;
  struct buf *prev = 0;

  struct buf *best = 0;
  struct buf *best_prev = 0;
  int best_prio = 4;

  // iterate through the queue
  while (curr != 0) {
    if (curr->priority < best_prio) {
      best_prio = curr->priority;
      best = curr;
      best_prev = prev;

      // Exit if first best priority found
      if (best_prio == 0) break;
    }
    prev = curr;
    curr = curr->qnext;
  }

  if (best == 0) return 0;

  if (best_prev == 0) {
    disk_queue[disk].head = best->qnext;
  } else {
    best_prev->qnext = best->qnext;
  }

  if (best == disk_queue[disk].tail) {
    disk_queue[disk].tail = best_prev;
  }

  return best;
}

// Select a buffer from a queue using SSTF, priority aware.
struct buf *select_sstf(int disk) {
  struct buf *curr = disk_queue[disk].head;
  struct buf *prev = 0;

  struct buf *best = 0;
  struct buf *best_prev = 0;

  int best_prio = 4;
  uint best_dist = 999999;

  // Iterate through the queue
  while (curr != 0) {
    // Find absolute dist from current head
    int dist = (int)disk_queue[disk].current_head - (int)curr->blockno;
    if (dist < 0)
      dist = -dist;

    // Check priority, higher priority is scheduled first
    // regardless of distance
    if ((curr->priority < best_prio) ||
        (curr->priority == best_prio && dist < best_dist)) {
      best_prio = curr->priority;
      best_dist = dist;
      best = curr;
      best_prev = prev;
    }

    prev = curr;
    curr = curr->qnext;
  }

  // Empty buffer
  if (best == 0) {
    return 0;
  }
  // Head case
  if (best_prev == 0) {
    disk_queue[disk].head = best->qnext;
  } else {
    best_prev->qnext = best->qnext;
  }
  // Tail case
  if (best == disk_queue[disk].tail) {
    disk_queue[disk].tail = best_prev;
  }

  return best;
}

// Asynchronously select buffers and dispatch disk reads/writes
int dispatch_disk_request(void) {

  struct buf *b = 0;
  uint latency;
  uint64 blockno = -1;
  int write = -1;
  //int priority = -1;
  uchar *data = 0;
  int q_id = 0;

  // Go through all 4 disks
  for (int i = 0; i < 4; i++) {
    // Start after the last served disk
    q_id = (last_serviced_disk + 1 + i) % 4;

    acquire(&disk_queue[q_id].lock);
    if (disk_queue[q_id].head == 0) {
      // If empty, skip
      release(&disk_queue[q_id].lock);
      continue;
    }

    last_serviced_disk = q_id;

    // Select the next buffer using FCFS or SSTF
    if (disk_sched_policy == 0) {
      b = select_fcfs(q_id);
    } else {
      b = select_sstf(q_id);
    }

    // Copy the data into local variables
    if (b != 0) {
      blockno = b->blockno;
      write = b->is_write;
      data = b->data;
      q_id = b->queue_id;
      // priority = b->priority;
    }

    // Calculate the latency
    int diff = disk_queue[q_id].current_head - blockno;
    if (diff < 0)
      diff = -diff;
    latency = diff + DISK_C;

    // Change current head request
    disk_queue[q_id].current_head = blockno;

    // Debug print
    //printf("[DISPATCH] Disk: %d | Block: %ld | Prio: %d | Latency: %d\n", q_id,
    //       blockno, priority, latency);

    release(&disk_queue[q_id].lock);
    break;
  }

  if (b == 0)
    return 0;

  // Find the owner process and update statistics
  struct proc *p = get_proc_by_pid(b->pid);
  if(p){
    if(write) p->disk_writes++;
    else p->disk_reads++;
    p->total_latency += latency;
  }

  // Calculate the actual physical block to access
  // based on RAID level
  uint64 physical_block = 0;
  if (blockno < SWAP_START) {
    // If the block is before swap space
    physical_block = blockno;
  } else {

    uint norm_block = blockno - SWAP_START;

    if (raid_level == RAID_0) {
      int target_disk = norm_block % 4;
      int local_block = norm_block / 4;
      physical_block = SWAP_START + RAID_DISK_SIZE * target_disk + local_block;
    } else if (raid_level == RAID_1) {
      physical_block = SWAP_START + (RAID_DISK_SIZE * q_id) + norm_block;
    } else if (raid_level == RAID_5) {
      uint stripe = norm_block / 3;
      physical_block = SWAP_START + (RAID_DISK_SIZE * q_id) + stripe;
    }
  }
  uint64 sector = physical_block * (BSIZE / 512);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while (1) {
    if (alloc3_desc(idx) == 0) {
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if (write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64)buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64)data;
  disk.desc[idx[1]].len = BSIZE;
  if (write)
    disk.desc[idx[1]].flags = 0; // device reads b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; // device writes 0 on success
  disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  disk.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  return 1;
}

// Submit the buffer to the appropriate disk queue and wait
int submit_and_wait(struct buf *b, int q_id) {
  acquire(&disk_queue[q_id].lock);
  b->disk = 1;
  b->qnext = 0;
  b->queue_id = q_id;
  if (disk_queue[q_id].head == 0) {
    disk_queue[q_id].head = b;
    disk_queue[q_id].tail = b;
  } else {
    disk_queue[q_id].tail->qnext = b;
    disk_queue[q_id].tail = b;
  }
  release(&disk_queue[q_id].lock);

  acquire(&disk.vdisk_lock);
  if (disk_busy == 0) {
    disk_busy = 1;
    if (dispatch_disk_request() == 0)
      disk_busy = 0;
  }
  while (b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }
  release(&disk.vdisk_lock);
  return b->status;
}

void virtio_disk_rw(struct buf *b, int write) {
  // Fill in the struct
  uint64 block_no = b->blockno;
  b->is_write = write;
  b->disk = 1;
  b->qnext = 0;
  b->priority = myproc()->priority_level;
  b->pid = myproc()->pid;

  // Calculate the target based on RAID level
  if (raid_level == RAID_0) {

    int target_disk;

    if (b->blockno < SWAP_START) {
      target_disk = 0;
    } else {
      uint norm_block = b->blockno - SWAP_START;
      target_disk = norm_block % 4;
    }

    submit_and_wait(b, target_disk);

  } else if (raid_level == RAID_1) {
    int primary;

    if (block_no < SWAP_START) {
      primary = 0;
    } else {
      primary = (block_no % 2 == 0) ? 0 : 2;
    }

    submit_and_wait(b, primary);

    if (write) {
      b->disk = 1;
      int secondary = primary + 1;
      submit_and_wait(b, secondary);
    }
  } else if (raid_level == RAID_5) {
    if (block_no < SWAP_START) {
      submit_and_wait(b, 0);
      return;
    }

    int target_disk, parity_disk, stripe, slot;
    uint norm_block = block_no - SWAP_START;
    stripe = norm_block / 3;
    slot = norm_block % 3;
    parity_disk = stripe % 4;
    target_disk = (slot >= parity_disk) ? slot + 1 : slot;

    // If writing in RAID 5, then we need to fetch
    // the old data and parity and update the new parity
    if (write) {
      // Allocate a 4KB page to store the data
      uchar *scratch = kalloc();
      if (scratch == 0)
        panic("RAID5: kalloc failed");

      // Allocate memory
      uchar *oldData = scratch;
      uchar *oldParity = scratch + BSIZE;
      uchar *newParity = scratch + (2 * BSIZE);

      // Temp buffer to use for data access
      struct buf temp;
      memset(&temp, 0, sizeof(temp));

      // For RAID 5 we just pass the stripe as our block number (added to swap start)
      // Fetch the data and store it in our page
      temp.is_write = 0;
      temp.blockno = block_no;
      temp.disk = 1;
      submit_and_wait(&temp, target_disk);
      memmove(oldData, temp.data, BSIZE);

      memset(&temp, 0, sizeof(temp));
      temp.is_write = 0;
      temp.blockno = block_no;
      temp.disk = 1;
      submit_and_wait(&temp, parity_disk);
      memmove(oldParity, temp.data, BSIZE);

      // Calculate the new parity
      for (int i = 0; i < BSIZE; i++) {
        newParity[i] = oldParity[i] ^ oldData[i] ^ (b->data)[i];
      }

      // Store it back
      memset(&temp, 0, sizeof(temp));
      memmove(temp.data, newParity, BSIZE);
      temp.blockno = block_no;
      temp.is_write = 1;
      temp.disk = 1;
      submit_and_wait(&temp, parity_disk);

      // Free the scratchpad memory
      kfree(scratch);
    }

    if (submit_and_wait(b, target_disk) != 0) {
      // Reconstruction, allocate 4KB to load data from the other disks
      uchar *scratch = kalloc();
      if (scratch == 0)
        panic("RAID5: kalloc failed");

      uchar *parity = scratch;
      uchar *nbr1 = scratch + BSIZE;
      uchar *nbr2 = scratch + (2 * BSIZE);

      // Find the other two neighbours
      uint d1 = -1, d2 = -1;
      for (int i = 0; i < 4; i++) {
        if (i != target_disk && i != parity_disk) {
          if (d1 == -1) {
            d1 = i;
          } else {
            d2 = i;
            break;
          }
        }
      }

      // Fetch data from the other disks
      struct buf temp;
      memset(&temp, 0, sizeof(temp));

      temp.is_write = 0;
      temp.blockno = block_no;
      temp.disk = 1;
      submit_and_wait(&temp, parity_disk);
      memmove(parity, temp.data, BSIZE);
      memset(&temp, 0, sizeof(temp));

      temp.is_write = 0;
      temp.blockno = block_no;
      temp.disk = 1;
      submit_and_wait(&temp, d1);
      memmove(nbr1, temp.data, BSIZE);
      memset(&temp, 0, sizeof(temp));

      temp.is_write = 0;
      temp.blockno = block_no;
      temp.disk = 1;
      submit_and_wait(&temp, d2);
      memmove(nbr2, temp.data, BSIZE);

      // Reconstruct the lot data
      for (int i = 0; i < BSIZE; i++) {
        (b->data)[i] = parity[i] ^ nbr1[i] ^ nbr2[i];
      }

      // Free the scratchpad memory
      kfree(scratch);
    }
  }
}

void virtio_disk_intr() {
  acquire(&disk.vdisk_lock);
  int q_id = -1;

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while (disk.used_idx != disk.used->idx) {
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    // if(disk.info[id].status != 0)
    //   panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->status = disk.info[id].status;
    b->disk = 0; // disk is done with buf

    q_id = b->queue_id;

    disk.info[id].b = 0;
    free_chain(id);

    wakeup(b);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);

  if (q_id != -1) {
    acquire(&disk.vdisk_lock);
    if (dispatch_disk_request() == 0) {
      disk_busy = 0;
    }
    release(&disk.vdisk_lock);
  }
}
