#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 100 // Deliberately higher than MAX_USER_FRAMES
#define INTS_PER_PAGE (PGSIZE / sizeof(int))

// Ensure this matches your kernel struct exactly
struct vmstats;

// Helper function to print the difference between two stat snapshots
void print_telemetry(char *phase_name, struct vmstats *before, struct vmstats *after) {
    printf("\n--- %s Telemetry ---\n", phase_name);
    printf("Page Faults: %d\n", after->page_faults - before->page_faults);
    printf("Evictions:   %d\n", after->pages_evicted - before->pages_evicted);
    printf("Swap-Ins:    %d\n", after->pages_swapped_in - before->pages_swapped_in);
    printf("[State] Resident: %d | Swapped Out: %d\n", after->resident_pages, after->pages_swapped_out);
    printf("------------------------------------\n");
}

int main(int argc, char *argv[]) {
    printf("=== Correctness & Data Integrity Test ===\n\n");

    char *mem[NUM_PAGES];
    int pid = getpid();
    struct vmstats st_start, st_alloc, st_write, st_seq, st_rev;

    // Grab baseline
    getvmstats(pid, &st_start);

    // --- PHASE 1: Allocation ---
    printf("Phase 1: Allocating %d pages...\n", NUM_PAGES);
    for(int i = 0; i < NUM_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        if(mem[i] == 0) {
            printf("FATAL: malloc failed at page %d\n", i);
            exit(1);
        }
    }
    getvmstats(pid, &st_alloc);
    print_telemetry("Post-Allocation", &st_start, &st_alloc);

    // --- PHASE 2: Deep Memory Write ---
    printf("\nPhase 2: Deep Memory Write...\n");
    for(int i = 0; i < NUM_PAGES; i++) {
        int *page_ptr = (int *)mem[i];
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            page_ptr[j] = (i * 10000) + j;
        }
    }
    getvmstats(pid, &st_write);
    print_telemetry("Post-Write", &st_alloc, &st_write);

    // --- PHASE 3: Sequential Verification ---
    printf("\nPhase 3: Sequential Deep Read & Verify...\n");
    for(int i = 0; i < NUM_PAGES; i++) {
        int *page_ptr = (int *)mem[i];
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            int expected = (i * 10000) + j;
            if(page_ptr[j] != expected) {
                printf("\n>>> CORRUPTION DETECTED <<<\n");
                printf("Page: %d, Offset: %d\n", i, j);
                exit(1);
            }
        }
    }
    getvmstats(pid, &st_seq);
    print_telemetry("Post-Sequential Read", &st_write, &st_seq);

    // --- PHASE 4: Reverse Thrashing ---
    printf("\nPhase 4: Reverse Deep Read (Thrashing)...\n");
    for(int i = NUM_PAGES - 1; i >= 0; i--) {
        int *page_ptr = (int *)mem[i];
        for(int j = 0; j < INTS_PER_PAGE; j++) {
            int expected = (i * 10000) + j;
            if(page_ptr[j] != expected) {
                printf("\n>>> CORRUPTION DETECTED ON REVERSE PASS <<<\n");
                printf("Page: %d, Offset: %d\n", i, j);
                exit(1);
            }
        }
    }
    getvmstats(pid, &st_rev);
    print_telemetry("Post-Reverse Read", &st_seq, &st_rev);

    printf("\nRESULT: PASS! 100%% Data Integrity Maintained Across All Blocks.\n");
    exit(0);
}
