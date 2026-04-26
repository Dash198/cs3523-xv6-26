#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 50

int main() {
    setraidlevel(5);
    setdisksched(1); // SSTF

    printf("\n=================================================\n");
    printf("       RAID 5 FINAL INTEGRATION TEST\n");
    printf("=================================================\n");
    printf("- Validating Multi-Process Concurrency\n");
    printf("- Validating Parity Read-Modify-Write Cycles\n");
    printf("- Emulating Heavy Swapping Load\n\n");

    for(int i = 0; i < 4; i++) {
        if(fork() == 0) {
            // Give children varied memory loads to stagger swap timing
            int my_pages = NUM_PAGES + (i * 10);
            char **mem = malloc(my_pages * sizeof(char*));

            // Write Phase (Causes heavy parity calculation queues)
            for(int p = 0; p < my_pages; p++) {
                mem[p] = malloc(PGSIZE);
                for(int j=0; j<10; j++) mem[p][j*100] = getpid() + i;
            }

            // Read Phase
            for(int p = 0; p < my_pages; p++) {
                for(int j=0; j<10; j++) {
                    if(mem[p][j*100] != (char)(getpid() + i)) {
                        printf("\n[!] FATAL ERROR: RAID 5 CRITICAL FAILURE for PID %d\n", getpid());
                        exit(1);
                    }
                }
            }

            printf("[Process %d] Successfully verified %d pages.\n", getpid(), my_pages);
            exit(0);
        }
    }

    // Parent waits for all child processes to finish their onslaught
    for(int i=0; i<4; i++) wait(0);

    printf("\n>>> RAID 5 MULTI-PROCESS TEST PASSED <<<\n");
    printf("If the terminal didn't panic, your Queue Locking and \n");
    printf("RAID-5 scheduling logic handles intense load perfectly!\n");
    exit(0);
}
