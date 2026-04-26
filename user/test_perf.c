#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 70
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

void run_scatter_workload() {
    int *mem[NUM_PAGES];

    // Allocate & Write (sequential eviction)
    for(int i = 0; i < NUM_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        for(int j = 0; j < INTS_PER_PAGE; j+=256) {
            mem[i][j] = i;
        }
    }

    // SCATTER READ PATTERN (Bouncing back and forth across swap blocks)
    // For 60 pages, we read 0, 59, 1, 58, 2, 57...
    // This creates massive disk head movement for FCFS!
    for(int i = 0; i < NUM_PAGES / 2; i++) {
        volatile int a = mem[i][0];
        volatile int b = mem[NUM_PAGES - 1 - i][0];
        (void)a; (void)b;
    }
}

int main() {
    int pid;
    struct vmstats st;

    printf("\n=================================================\n");
    printf("   DISK SCHEDULER PERFORMANCE (FCFS vs SSTF)\n");
    printf("=================================================\n");
    printf("This test scatters disk read requests to maximize\n");
    printf("disk head movement and demonstrate SSTF efficiency.\n\n");

    for(int policy = 0; policy <= 1; policy++) {
        setdisksched(policy);
        printf("--- Running With %s Scheduler ---\n", policy ? "SSTF" : "FCFS");

        if((pid = fork()) == 0) {
            // Interactive priority boost
            for(int i=0; i<10000; i++) getpid();

            run_scatter_workload();

            if(getvmstats(getpid(), &st) == 0) {
                printf("Test complete for %s!\n", policy ? "SSTF" : "FCFS");
                printf("-> Total Disk Ops:  %d\n", st.disk_reads + st.disk_writes);
                printf("-> Average Latency: %d ticks\n\n", st.average_latency);
            }
            exit(0);
        }
        wait(0);
    }

    printf(">>> PERFORMANCE TEST COMPLETE <<<\n");
    printf("(SSTF Avg Latency should be significantly lower than FCFS)\n");
    exit(0);
}
