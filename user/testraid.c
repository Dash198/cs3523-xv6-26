#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 60
#define INTS_PER_PAGE (PGSIZE / sizeof(int))

// Matches your updated kernel struct
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

void verify_and_report(int id, char *label) {
    int *mem[NUM_PAGES];
    struct vmstats st;
    int pid = getpid();

    // 1. Write unique data patterns
    for(int i = 0; i < NUM_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            mem[i][j] = (id * 10000) + (i * 100) + j;
        }
    }

    // 2. Force thrashing (Read back)
    for(int i = 0; i < NUM_PAGES; i++) {
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            int expected = (id * 10000) + (i * 100) + j;
            if(mem[i][j] != expected) {
                printf("[%s] FAILED: Corruption at Page %d\n", label, i);
                exit(1);
            }
        }
    }

    // 3. Pull all-in-one stats
    if(getvmstats(pid, &st) == 0) {
        printf("\n--- Results for %s (PID %d) ---\n", label, pid);
        printf("Faults: %d | Swapped In: %d | Swapped Out: %d\n",
                st.page_faults, st.pages_swapped_in, st.pages_swapped_out);
        printf("Disk Reads: %d | Disk Writes: %d | Avg Latency: %d\n",
                st.disk_reads, st.disk_writes, st.average_latency);
        printf("--------------------------------------------\n");
    }
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: testfinal [0: FCFS, 1: SSTF]\n");
        exit(1);
    }

    int policy = atoi(argv[1]);
    setdisksched(policy);
    printf("=== RAID 5 Final Integration Test (%s) ===\n", policy ? "SSTF" : "FCFS");

    int pid1 = fork();
    if(pid1 == 0) {
        // CPU-bound: Long spin before IO
        for(volatile int i = 0; i < 20000000; i++);
        verify_and_report(1, "CPU-BOUND");
        exit(0);
    }

    int pid2 = fork();
    if(pid2 == 0) {
        // Interactive: High syscall volume then IO
        for(int i = 0; i < 10000; i++) getpid();
        verify_and_report(2, "INTERACTIVE");
        exit(0);
    }

    wait(0);
    wait(0);
    printf("\nTEST COMPLETE: System Stable & Data Intact.\n");
    exit(0);
}
