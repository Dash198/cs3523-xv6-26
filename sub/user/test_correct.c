#include "kernel/types.h"
#include "user/user.h"

// Multi-eviction correctness test.
// Allocates 100 pages (> MAX_USER_FRAMES=64) with a unique deterministic
// pattern. Then reads them back in REVERSE order so every access is a
// swap-in that may trigger another eviction — ensuring each page
// survives multiple evict/swap_out/swap_in cycles without corruption.

#define PGSIZE        4096
#define INTS_PER_PAGE (PGSIZE / sizeof(int))
#define TOTAL_PAGES   100

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
  printf("    MULTI-EVICTION SWAP CORRECTNESS TEST\n");
  printf("=================================================\n");
  printf("- %d pages written sequentially (> MAX_USER_FRAMES)\n", TOTAL_PAGES);
  printf("- Read-back in REVERSE order — maximum swap-in pressure\n");
  printf("- Every integer word verified for corruption\n\n");

  int pid = getpid();
  int *mem[TOTAL_PAGES];

  // ---- Write phase ----
  printf("[1] Writing pages with unique pattern...\n");
  for (int i = 0; i < TOTAL_PAGES; i++) {
    mem[i] = malloc(PGSIZE);
    for (int j = 0; j < INTS_PER_PAGE; j++)
      mem[i][j] = (i * INTS_PER_PAGE) + j + 1; // never zero
  }

  // ---- Reverse read-back ----
  printf("[2] Reading back in reverse order...\n");
  int errors = 0;
  for (int i = TOTAL_PAGES - 1; i >= 0; i--) {
    for (int j = 0; j < INTS_PER_PAGE; j++) {
      int expected = (i * INTS_PER_PAGE) + j + 1;
      if (mem[i][j] != expected) {
        printf("[ERROR] Page=%d word=%d  got=%d  expected=%d\n",
               i, j, mem[i][j], expected);
        errors++;
        if (errors > 5) goto done; // cap output
      }
    }
  }

done:;
  struct vmstats st;
  getvmstats(pid, &st);

  printf("\n--- Test Metrics ---\n");
  printf("Page Faults  : %d\n", st.page_faults);
  printf("Swapped Out  : %d\n", st.pages_swapped_out);
  printf("Swapped In   : %d\n", st.pages_swapped_in);
  printf("Disk Reads   : %d blocks\n", st.disk_reads);
  printf("Disk Writes  : %d blocks\n", st.disk_writes);
  printf("Avg Latency  : %d ticks\n",  st.average_latency);
  printf("Resident Now : %d pages\n",  st.resident_pages);

  printf("\n[Analysis]\n");
  printf("Swapped-out should be >= %d (pages beyond frame limit).\n",
         TOTAL_PAGES - 64);
  printf("Swapped-in should be roughly equal to Swapped-out.\n");

  if (errors == 0)
    printf("\n>>> SWAP CORRECTNESS TEST PASSED <<<\n");
  else
    printf("\n>>> SWAP CORRECTNESS TEST FAILED (%d errors) <<<\n", errors);

  exit(0);
}
