#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 70 // Must be greater than MAX_USER_FRAMES (64) to force evictions
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

void run_integrity(int level) {
    setraidlevel(level);
    printf("\n=================================================\n");
    printf("   RAID %d INTEGRITY & AMPLIFICATION TEST\n", level);
    printf("=================================================\n");
    if(level == 0) printf("- Expecting 1 write per block (Striping)\n");
    if(level == 1) printf("- Expecting 2 writes per block (Mirroring)\n");

    int pid = getpid();
    int *mem[NUM_PAGES];

    printf("\n[1] Allocating %d pages and writing data...\n", NUM_PAGES);
    for(int i = 0; i < NUM_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            mem[i][j] = (level * 100000) + (i * 100) + j;
        }
    }

    printf("[2] Verifying data back from swap...\n");
    for(int i = 0; i < NUM_PAGES; i++) {
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            int expected = (level * 100000) + (i * 100) + j;
            if(mem[i][j] != expected) {
                printf("[ERROR] DATA CORRUPTION at RAID %d, Page %d\n", level, i);
                exit(1);
            }
        }
    }

    struct vmstats st;
    if(getvmstats(pid, &st) == 0) {
        printf("\n--- Test Metrics for RAID %d ---\n", level);
        printf("Swapped Out Pages: %d\n", st.pages_swapped_out);
        printf("Swapped In Pages:  %d\n", st.pages_swapped_in);
        printf("Total Disk Reads:  %d Disk blocks\n", st.disk_reads);
        printf("Total Disk Writes: %d Disk blocks\n", st.disk_writes);

        // Let's analyze write amplification
        int base_writes = st.pages_swapped_out * 4; // BSIZE = 1024, PGSIZE = 4096 => 4 blocks/page
        printf("\n[Analysis]\n");
        printf("Base filesystem writes needed: %d blocks\n", base_writes);
        if(level == 1) {
            printf("RAID 1 amplification observed: %d writes (Expected: ~%d)\n", st.disk_writes, base_writes * 2);
        } else if (level == 0) {
            printf("RAID 0 amplification observed: %d writes (Expected: ~%d)\n", st.disk_writes, base_writes);
        }
    }
    printf("\n>>> RAID %d TEST PASSED <<<\n", level);
}

int main() {
    int pid = fork();
    if(pid == 0) {
        run_integrity(0); // Test Striping
        exit(0);
    }
    wait(0);

    pid = fork();
    if(pid == 0) {
        run_integrity(1); // Test Mirroring
        exit(0);
    }
    wait(0);

    exit(0);
}
