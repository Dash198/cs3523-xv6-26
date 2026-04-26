#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NUM_PAGES 40
#define PAGE_SIZE 4096

// --- The Priority Manipulators ---

void make_low_priority() {
    // Burn CPU time to increase 'dt' without increasing 'ds'
    // This forces the PA2 scheduler to demote the process
    for(volatile int i = 0; i < 50000000; i++) {
        // Do nothing, just spin
    }
}

void make_high_priority() {
    // Spam system calls to massively increase 'ds' in very little 'dt'
    // This forces the PA2 scheduler to promote the process as "interactive"
    for(int i = 0; i < 50000; i++) {
        getpid();
    }
}

// --- The Payload ---

void trigger_page_faults(char id) {
    char *mem[NUM_PAGES];

    // Phase 1: Allocate and write (Forces swap-outs)
    for(int i = 0; i < NUM_PAGES; i++){
        mem[i] = malloc(PAGE_SIZE);
        mem[i][0] = id;
    }

    // Phase 2: Read back (Forces swap-ins)
    for(int i = 0; i < NUM_PAGES; i++){
        volatile char c = mem[i][0];
        (void)c;
    }
}

int main(int argc, char *argv[]) {
    if(argc < 2){
        printf("Usage: testsched [0 for FCFS, 1 for SSTF]\n");
        exit(1);
    }

    int policy = atoi(argv[1]);
    setdisksched(policy);
    printf("\n--- Starting Behavioral Test with Policy: %s ---\n", policy == 1 ? "SSTF" : "FCFS");

    // Spawn Child 1 (CPU-Bound / Low Priority)
    if(fork() == 0) {
        make_low_priority();
        trigger_page_faults('A');
        exit(0);
    }

    // Spawn Child 2 (CPU-Bound / Low Priority)
    if(fork() == 0) {
        make_low_priority();
        trigger_page_faults('B');
        exit(0);
    }

    // Spawn Child 3 (Interactive / High Priority)
    if(fork() == 0) {
        make_low_priority();  // Sync up the timing with the others first
        make_high_priority(); // Spike the syscalls right before faulting
        trigger_page_faults('C');
        exit(0);
    }

    // Parent waits for the children to finish
    wait(0);
    wait(0);
    wait(0);

    printf("\n--- Test Complete ---\n");
    exit(0);
}
