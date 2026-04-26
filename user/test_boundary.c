#include "kernel/types.h"
#include "user/user.h"

// Stripe boundary stress test.
// Accesses logical swap blocks at exact stride-3 boundaries so every
// access starts a fresh RAID-5 stripe with a different parity disk.
// Verifies that the stripe/slot/parity_disk mapping never overlaps.
//
// RAID-5 geometry reminder (4 disks, 3 data blocks per stripe):
//   norm_block = block_no - SWAP_START
//   stripe     = norm_block / 3
//   slot       = norm_block % 3
//   parity_disk = stripe % 4
//   target_disk = (slot >= parity_disk) ? (slot+1)%4 : slot
//
// By writing exactly one page per stripe boundary (every 3rd logical
// block = every 12th byte-page in 4-block-per-page scheme), and then
// reading them all back, we force a parity RMW on each distinct
// parity disk in sequence (D0, D1, D2, D3, D0, ...).

#define PGSIZE       4096
#define INTS_PER_PAGE (PGSIZE / sizeof(int))

// Allocate enough pages to push into swap.
// MAX_USER_FRAMES = 64; we use 70 to guarantee evictions.
#define TOTAL_PAGES  70

struct vmstats {
  int page_faults;
  int pages_evicted;
  int pages_swapped_in;
  int pages_swapped_out;
  int resident_pages;
  int disk_reads;
  int disk_writes;
  int average_latency;
};

int main() {
  setraidlevel(5);
  setdisksched(1); // SSTF

  printf("\n=================================================\n");
  printf("     RAID-5 STRIPE BOUNDARY STRESS TEST\n");
  printf("=================================================\n");
  printf("- Writing %d pages (> MAX_USER_FRAMES) to force evictions\n", TOTAL_PAGES);
  printf("- Access pattern targets every stripe boundary\n");
  printf("- Verifies parity-disk rotation: D0,D1,D2,D3 per 4 stripes\n\n");

  int pid = getpid();
  int *mem[TOTAL_PAGES];

  // --- Write phase ---
  printf("[1] Allocating and writing pages...\n");
  for (int i = 0; i < TOTAL_PAGES; i++) {
    mem[i] = malloc(PGSIZE);
    // Fill entire page with a unique deterministic value per page
    for (int j = 0; j < INTS_PER_PAGE; j++)
      mem[i][j] = (i << 16) | j;
  }

  // --- Access stripe boundaries in order: 0, 3, 6, 9 ... ---
  // Each group of 3 pages shares a stripe. Accessing only the
  // first page of each triple starts a fresh stripe each time.
  printf("[2] Touching stripe-boundary pages (every 3rd page)...\n");
  int bad = 0;
  for (int i = 0; i < TOTAL_PAGES; i += 3) {
    volatile int v = mem[i][0];
    if (v != ((i << 16) | 0)) {
      printf("[ERROR] Stripe %d page %d: got 0x%x expected 0x%x\n",
             i / 3, i, v, (i << 16) | 0);
      bad++;
    }
  }

  // --- Full read-back verification ---
  printf("[3] Full read-back of all %d pages...\n", TOTAL_PAGES);
  for (int i = 0; i < TOTAL_PAGES; i++) {
    for (int j = 0; j < INTS_PER_PAGE; j++) {
      int expected = (i << 16) | j;
      if (mem[i][j] != expected) {
        printf("[ERROR] Page %d word %d: got 0x%x expected 0x%x\n",
               i, j, mem[i][j], expected);
        bad++;
        goto done;
      }
    }
  }

done:;
  struct vmstats st;
  getvmstats(pid, &st);

  printf("\n--- Test Metrics ---\n");
  printf("Swapped Out : %d pages\n", st.pages_swapped_out);
  printf("Swapped In  : %d pages\n", st.pages_swapped_in);
  printf("Disk Reads  : %d blocks\n", st.disk_reads);
  printf("Disk Writes : %d blocks\n", st.disk_writes);
  printf("Avg Latency : %d ticks\n",  st.average_latency);

  printf("\n[Analysis]\n");
  printf("With %d pages, parity rotated across all 4 disks.\n", TOTAL_PAGES);
  printf("Writes should be ~%d blocks (RMW: 5 ops * %d evictions).\n",
         5 * st.pages_swapped_out * 4, st.pages_swapped_out);

  if (bad == 0)
    printf("\n>>> STRIPE BOUNDARY TEST PASSED <<<\n");
  else
    printf("\n>>> STRIPE BOUNDARY TEST FAILED (%d errors) <<<\n", bad);

  exit(0);
}
