#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 100 // Deliberately higher than MAX_USER_FRAMES

// Make sure this matches your kernel struct exactly!
struct vmstats;

int main(int argc, char *argv[]) {
    printf("Starting brutal swap test with telemetry...\n\n");

    char *mem[NUM_PAGES];
    struct vmstats before, after;
    int pid = getpid();

    // 1. Take the baseline snapshot
    if(getvmstats(pid, &before) < 0) {
        printf("Error: getvmstats syscall failed!\n");
        exit(1);
    }

    // --- PHASE 1: The Eviction Wave ---
    printf("Allocating and writing to %d pages...\n", NUM_PAGES);
    for(int i = 0; i < NUM_PAGES; i++){
        mem[i] = malloc(PGSIZE);
        if(mem[i] == 0){
            printf("TEST FAILED: malloc failed at page %d\n", i);
            exit(1);
        }

        int *ptr = (int *)mem[i];
        *ptr = i;
    }
    printf("Write phase complete. Memory should be heavily swapped.\n\n");


    // --- PHASE 2: The Resurrection ---
    printf("Reading back data (forcing massive swap-ins)...\n");
    for(int i = 0; i < NUM_PAGES; i++){
        int *ptr = (int *)mem[i];

        if(*ptr != i){
            printf("TEST FAILED: Data corruption at page %d! Expected %d, got %d\n", i, i, *ptr);
            exit(1);
        }
    }

    // 2. Take the aftermath snapshot
    getvmstats(pid, &after);

    printf("\nTEST PASSED! All data survived the round trip to the shadow realm.\n");

    // 3. Print the Telemetry Data
    printf("\n================ SWAP TELEMETRY ================\n");
    // Cumulative events (Deltas)
    printf("Total Page Faults Triggered: %d\n", after.page_faults - before.page_faults);
    printf("Total Pages Evicted to Disk: %d\n", after.pages_evicted - before.pages_evicted);
    printf("Total Pages Rescued (Swapped In): %d\n", after.pages_swapped_in - before.pages_swapped_in);
    printf("------------------------------------------------\n");
    // Current state (Absolute)
    printf("Current Pages Parked in Swap: %d\n", after.pages_swapped_out);
    printf("Current Pages Resident in RAM:  %d\n", after.resident_pages);
    printf("================================================\n\n");

    exit(0);
}
