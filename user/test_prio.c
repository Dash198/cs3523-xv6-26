#include "kernel/types.h"
#include "user/user.h"

// Tests that SSTF respects MLFQ priority levels.
// An I/O-bound child stays at high priority (level 0/1) while
// a CPU-bound child degrades to level 3. Under SSTF the I/O-bound
// child should see lower average disk latency than the CPU-bound one.

#define PGSIZE       4096
#define NUM_PAGES    60    // > MAX_USER_FRAMES to force evictions

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

// Burn CPU to sink the process into a low MLFQ priority tier.
void burn_cpu(int iters) {
  volatile int x = 0;
  for (int i = 0; i < iters; i++) x += i;
  (void)x;
}

int main() {
  setraidlevel(5);
  setdisksched(1); // SSTF

  printf("\n=================================================\n");
  printf("     PRIORITY-AWARE DISK SCHEDULING TEST\n");
  printf("=================================================\n");
  printf("- I/O-bound child  -> stays at high MLFQ priority\n");
  printf("- CPU-bound child  -> sinks to low  MLFQ priority\n");
  printf("- Scheduler used   -> SSTF (priority-sensitive)\n\n");

  // ----- Child A: I/O-bound (high priority) -----
  int pid_a = fork();
  if (pid_a == 0) {
    // Yield frequently to stay at the top of the MLFQ
    for (int i = 0; i < 50; i++) pause(1);

    int *mem[NUM_PAGES];
    for (int i = 0; i < NUM_PAGES; i++) {
      mem[i] = malloc(PGSIZE);
      mem[i][0] = i;
    }
    // Touch every page in forward order (swap-ins)
    for (int i = 0; i < NUM_PAGES; i++) {
      volatile int x = mem[i][0]; (void)x;
    }

    struct vmstats st;
    getvmstats(getpid(), &st);
    printf("[IO-BOUND  PID %d] average_latency=%d  disk_reads=%d\n",
           getpid(), st.average_latency, st.disk_reads);
    exit(0);
  }

  // ----- Child B: CPU-bound (low priority) -----
  int pid_b = fork();
  if (pid_b == 0) {
    // Burn CPU aggressively to sink to MLFQ level 3
    burn_cpu(5000000);

    int *mem[NUM_PAGES];
    for (int i = 0; i < NUM_PAGES; i++) {
      mem[i] = malloc(PGSIZE);
      mem[i][0] = i + 1000;
    }
    for (int i = 0; i < NUM_PAGES; i++) {
      volatile int x = mem[i][0]; (void)x;
    }

    struct vmstats st;
    getvmstats(getpid(), &st);
    printf("[CPU-BOUND PID %d] average_latency=%d  disk_reads=%d\n",
           getpid(), st.average_latency, st.disk_reads);
    exit(0);
  }

  wait(0); wait(0);

  printf("\n[Analysis]\n");
  printf("If SSTF is priority-aware, the IO-BOUND process\n");
  printf("should report a lower average_latency than CPU-BOUND.\n");
  printf(">>> PRIORITY SCHEDULING TEST COMPLETE <<<\n");
  exit(0);
}
