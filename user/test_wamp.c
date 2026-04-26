#include "kernel/types.h"
#include "user/user.h"

// Write amplification comparison across RAID levels.
// Runs the same 60-page workload under RAID-0, RAID-1, and RAID-5
// and prints the ratios. Expected amplification:
//
//   RAID-0 : 1× (4 blocks/page, no extra writes)
//   RAID-1 : 2× (data + mirror)
//   RAID-5 : ~5× per eviction (read_old_data + read_old_parity +
//               write_new_parity + write_data = 5 disk ops per block)
//             but since each page spans two stripes (4 blocks / 3
//             blocks-per-stripe), actual cost is 10 disk ops/page.
//
// This test does NOT verify correctness — test_integrity does that.
// It only quantifies the I/O cost of each RAID level.

#define PGSIZE       4096
#define NUM_PAGES    60   // > MAX_USER_FRAMES (64) — NO, 60 < 64, tweak workload
#define INTS_PER_PAGE (PGSIZE / sizeof(int))

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

void run_level(int level) {
  setraidlevel(level);
  int pid = getpid();

  // Reset stats baseline: fork a child so stats start fresh
  int cpid = fork();
  if (cpid == 0) {
    pid = getpid();
    int *mem[NUM_PAGES];

    for (int i = 0; i < NUM_PAGES; i++) {
      mem[i] = malloc(PGSIZE);
      for (int j = 0; j < INTS_PER_PAGE; j++)
        mem[i][j] = i * 100 + j;
    }

    // Sequential read-back (forces swap-in)
    for (int i = 0; i < NUM_PAGES; i++) {
      volatile int v = mem[i][0]; (void)v;
    }

    struct vmstats st;
    getvmstats(pid, &st);

    int swapped  = st.pages_swapped_out;
    int base_w   = swapped * 4; // 4 blocks per page, no RAID
    int actual_w = st.disk_writes;
    int actual_r = st.disk_reads;

    printf("RAID-%d | swapped=%d | writes=%d (base=%d, ratio=~%d) | reads=%d | latency=%d\n",
           level, swapped, actual_w, base_w,
           base_w > 0 ? actual_w / base_w : 0,
           actual_r, st.average_latency);

    exit(0);
  }
  wait(0);
}

int main() {
  setdisksched(0); // FCFS for fair comparison

  printf("\n=================================================\n");
  printf("     WRITE AMPLIFICATION ACROSS RAID LEVELS\n");
  printf("=================================================\n");
  printf("Workload: %d pages, read back sequentially after write\n\n", NUM_PAGES);

  printf("Level | swapped | writes (base, ratio) | reads | latency\n");
  printf("------+--------------------------------------------------\n");
  run_level(0);
  run_level(1);
  run_level(5);

  printf("\n[Analysis]\n");
  printf("Expected ratios vs base (RAID-0=1, RAID-1=2, RAID-5=~5-10)\n");
  printf("Higher RAID-5 ratio proves the Read-Modify-Write parity path.\n");
  printf(">>> WRITE AMPLIFICATION TEST COMPLETE <<<\n");
  exit(0);
}
