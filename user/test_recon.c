#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096
#define DATA_PAGES 20
#define FILL_PAGES 50 // Enough to push DATA_PAGES into swap (Total 70 > 64)

int main() {
    setraidlevel(5);
    setdisksched(1);

    char *mem[DATA_PAGES];

    printf("\n=================================================\n");
    printf("    RAID 5 RECONSTRUCTION (DEGRADED MODE)\n");
    printf("=================================================\n");

    // [1] Write unique patterns to the first batch of pages
    printf("[1] Writing patterns to %d pages...\n", DATA_PAGES);
    for(int i = 0; i < DATA_PAGES; i++) {
        mem[i] = malloc(PGSIZE);
        memset(mem[i], 'A' + i, PGSIZE);
    }

    // [2] Force Eviction
    // We allocate more memory to exceed MAX_USER_FRAMES (64)
    printf("[2] Allocating %d more pages to force RAID-5 swap-out...\n", FILL_PAGES);
    for(int i = 0; i < FILL_PAGES; i++) {
        char *p = malloc(PGSIZE);
        p[0] = i; // Touch it to ensure allocation
    }

    // [3] Fail the disk
    printf("[3] Simulating FAILURE of Physical Disk 1...\n");
    faildisk(1);

    // [4] Read back and verify
    // Every access here is a swap-in. If target_disk was Disk 1,
    // the XOR logic MUST kick in to return 'A' + i.
    printf("[4] Verifying data integrity via XOR reconstruction...\n");
    int success = 1;
    for(int i = 0; i < DATA_PAGES; i++) {
        if(mem[i][0] != 'A' + i) {
            printf("[FAIL] Corruption at Page %d! Expected %c, Got %c\n",
                   i, 'A' + i, mem[i][0]);
            success = 0;
            break;
        }
    }

    if(success) {
        printf("\n>>> RAID 5 RECONSTRUCTION SUCCESSFUL <<<\n");
        printf("All data recovered from neighbors despite Disk 1 failure.\n");
    }

    faildisk(-1); // Restore disk health
    exit(0);
}
