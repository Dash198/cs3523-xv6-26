#include "kernel/types.h"
#include "user/user.h"


void hog() {
    while(1) {
        // High-intensity math
        for (volatile int j = 0; j < 1000000; j++);
    }
}

int main() {
    int pids[6];
    printf("Spawning 6 Hogs on 3 Cores to force RUNNABLE states...\n");

    for(int i = 0; i < 6; i++) {
        int pid = fork();
        if(pid == 0) hog();
        pids[i] = pid;
    }

    struct mlfqinfo info;
    printf("Monitoring PIDs for Boost (128-tick interval)...\n");

    for(int t = 0; t < 40; t++) {
        pause(10);
        printf("\n--- Sample %d ---\n", t);
        for(int i = 0; i < 6; i++) {
            if(getmlfqinfo(pids[i], &info) == 0) {
                // Look for the jump from Level 3 back to Level 0
                printf("PID %d: L=%d | Ticks=[%d,%d,%d,%d]\n",
                       pids[i], info.level, info.ticks[0],
                       info.ticks[1], info.ticks[2], info.ticks[3]);
            }
        }
    }

    for(int i = 0; i < 6; i++) kill(pids[i]);
    while(wait(0) > 0);
    exit(0);
}
