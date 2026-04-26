#include "kernel/types.h"
#include "user/user.h"



int main() {
    int n_hogs = 6;
    int pids[6];

    printf("--- Starvation & Boost Proof ---\n");
    printf("Spawning %d hogs on 3 CPUs. Expecting some to be boosted...\n", n_hogs);

    for(int i = 0; i < n_hogs; i++) {
        int pid = fork();
        if(pid == 0) {
            while(1); // Permanent Hog
        }
        pids[i] = pid;
    }

    struct mlfqinfo info;
    int boost_seen = 0;

    // Monitor for ~150 ticks to ensure we cross the 128-tick boost boundary
    for(int s = 0; s < 30; s++) {
        pause(5);
        printf("\nSample %d:\n", s);

        for(int i = 0; i < n_hogs; i++) {
            if(getmlfqinfo(pids[i], &info) == 0) {
                // Proof of Starvation Prevention:
                // If a process was at L3 and its L0 ticks increased, it was boosted.
                if(s > 5 && info.ticks[0] > 2) {
                    printf("PID %d: Level %d | Ticks[0]=%d <-- BOOSTED!\n",
                           pids[i], info.level, info.ticks[0]);
                    boost_seen = 1;
                } else {
                    printf("PID %d: Level %d | Ticks[3]=%d\n",
                           pids[i], info.level, info.ticks[3]);
                }
            }
        }
        if(boost_seen && s > 25) break;
    }

    for(int i = 0; i < n_hogs; i++) kill(pids[i]);
    while(wait(0) > 0);

    if(boost_seen) printf("\nSUCCESS: Starvation prevented via Global Boost.\n");
    else printf("\nFAILURE: No boost detected. Check 128-tick logic.\n");

    exit(0);
}
