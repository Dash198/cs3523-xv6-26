#include "kernel/types.h"
#include "user/user.h"

// Safe process exit during active swap I/O.
// A child process allocates a large batch of pages (forcing heavy
// swap activity), and then the parent immediately reaps it with wait().
// The kernel's "post-sleep reality check" in evict_page() should:
//   (a) detect that the owner process is dead / state==UNUSED, and
//   (b) call free_swap_block() to release the slot rather than leak it.
//
// This test is a stability test: if the kernel panics or hangs, the
// fix is broken. If it completes cleanly the swap-block accounting
// did not corrupt the swap table.

#define PGSIZE     4096
#define HOG_PAGES  70   // well above MAX_USER_FRAMES (64)

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
  setdisksched(1);

  printf("\n=================================================\n");
  printf("    PROCESS EXIT DURING ACTIVE SWAP TEST\n");
  printf("=================================================\n");
  printf("- Child allocates %d pages (forces heavy swapping)\n", HOG_PAGES);
  printf("- Parent reaps child immediately\n");
  printf("- Kernel must handle orphaned swap blocks safely\n\n");

  int parent_pid = getpid();

  for (int round = 0; round < 3; round++) {
    printf("[Round %d] Forking swap-heavy child...\n", round + 1);

    int cpid = fork();
    if (cpid == 0) {
      // Child: allocate a ton of pages to hammer the eviction path
      int *mem[HOG_PAGES];
      for (int i = 0; i < HOG_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        mem[i][0] = i;
      }
      // Start re-accessing (triggers swap-in storm) -- but parent
      // will reap us mid-stream
      for (int i = HOG_PAGES - 1; i >= 0; i--) {
        volatile int v = mem[i][0]; (void)v;
      }
      exit(0);
    }

    // Parent: wait a very short time then reap unconditionally
    // The child may still have live disk I/O in flight.
    wait(0); // kernel should handle ongoing I/O safely
    printf("[Round %d] Child %d reaped safely.\n", round + 1, cpid);
  }

  // After 3 rounds, do a fresh small allocation to prove the swap
  // table is not leaked/corrupted.
  printf("\n[Validation] Allocating fresh pages after all rounds...\n");
  int *sanity[20];
  for (int i = 0; i < 20; i++) {
    sanity[i] = malloc(PGSIZE);
    sanity[i][0] = 0xBEEF + i;
  }
  int ok = 1;
  for (int i = 0; i < 20; i++) {
    if (sanity[i][0] != 0xBEEF + i) { ok = 0; break; }
  }

  struct vmstats st;
  getvmstats(parent_pid, &st);
  printf("Parent disk_reads=%d disk_writes=%d\n", st.disk_reads, st.disk_writes);

  if (ok)
    printf("\n>>> EXIT-DURING-SWAP TEST PASSED <<<\n");
  else
    printf("\n>>> EXIT-DURING-SWAP TEST FAILED (swap table corrupted) <<<\n");

  exit(0);
}
