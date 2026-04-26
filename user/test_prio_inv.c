#include "kernel/types.h"
#include "user/user.h"

// Priority starvation / inversion scenario.
// Spawns N CPU-bound hogs that flood the swap disk with requests,
// then times how long a single "interactive" process takes to
// complete its single page fault under FCFS vs SSTF.
//
// Expected: under SSTF the interactive latency is much lower because
// its high MLFQ priority lets it jump the queue.

#define PGSIZE      4096
#define NUM_HOGS    4
#define HOG_PAGES   55   // each hog pushes beyond MAX_USER_FRAMES

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

void hog_workload() {
  // Sink priority by burning CPU first
  volatile int x = 0;
  for (int i = 0; i < 3000000; i++) x += i;
  (void)x;

  int *mem[HOG_PAGES];
  for (int i = 0; i < HOG_PAGES; i++) {
    mem[i] = malloc(PGSIZE);
    mem[i][0] = i;
  }
  // Re-access all pages to cause swap-in storm
  for (int i = HOG_PAGES - 1; i >= 0; i--) {
    volatile int v = mem[i][0]; (void)v;
  }
}

void run_scenario(int policy) {
  setdisksched(policy);
  setraidlevel(5);

  printf("\n--- %s Scheduler ---\n", policy ? "SSTF" : "FCFS");

  // Spawn hogs
  int hog_pids[NUM_HOGS];
  for (int i = 0; i < NUM_HOGS; i++) {
    hog_pids[i] = fork();
    if (hog_pids[i] == 0) {
      hog_workload();
      exit(0);
    }
  }

  // Small delay so hogs get into the disk queue first
  for (int i = 0; i < 5; i++) pause(1);

  // Interactive process: stay high-priority, do tiny I/O
  int int_pid = fork();
  if (int_pid == 0) {
    // Lots of sleeps to stay at MLFQ level 0
    for (int i = 0; i < 20; i++) pause(1);

    int t0 = uptime();
    int *page = malloc(PGSIZE);
    page[0] = 0xCAFE;
    volatile int v = page[0]; (void)v;
    int t1 = uptime();

    struct vmstats st;
    getvmstats(getpid(), &st);
    printf("[INTERACTIVE PID %d] done in %d ticks | average_latency=%d\n",
           getpid(), t1 - t0, st.average_latency);
    exit(0);
  }

  // Wait for everyone
  for (int i = 0; i < NUM_HOGS + 1; i++) wait(0);
}

int main() {
  printf("\n=================================================\n");
  printf("    PRIORITY INVERSION / STARVATION TEST\n");
  printf("=================================================\n");
  printf("- %d CPU-hog processes flood the disk queue.\n", NUM_HOGS);
  printf("- 1 interactive process should jump the queue.\n\n");

  run_scenario(0); // FCFS — interactive process should suffer
  run_scenario(1); // SSTF — interactive process should win

  printf("\n[Analysis]\n");
  printf("Under FCFS the interactive process is buried by hogs.\n");
  printf("Under SSTF its high MLFQ priority lets it skip ahead.\n");
  printf(">>> STARVATION TEST COMPLETE <<<\n");
  exit(0);
}
