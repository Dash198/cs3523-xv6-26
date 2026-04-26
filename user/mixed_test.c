#include "kernel/types.h"
#include "user/user.h"

// Workload A: Purely compute-bound
void workload_compute() {
    while(1) {
        for (volatile int j = 0; j < 1000000; j++);
    }
}

// Workload B: Highly interactive (syscall-heavy)
void workload_interactive() {
    while(1) {
        for (int i = 0; i < 200; i++) getpid();
        for (volatile int j = 0; j < 100000; j++);
    }
}

int main() {
    int pids[5];
    struct mlfqinfo info;

    printf("--- MLFQ SCHEDULER EVALUATION ---\n");

    // Spawn 3 compute-bound processes (Demonstrates migration and starvation)
    for(int i = 0; i < 3; i++) {
        if((pids[i] = fork()) == 0) workload_compute();
    }

    // Spawn 1 interactive process (Demonstrates priority retention)
    if((pids[3] = fork()) == 0) workload_interactive();

    // Spawn 1 baseline compute process
    if((pids[4] = fork()) == 0) workload_compute();

    printf("Sampling metrics... (Interval: 5 ticks)\n");
    printf("Sample\tPID\tClass\tLevel\tTicks[L0, L1, L2, L3]\tSyscalls\n");

    for(int s = 0; s < 50; s++) {
        pause(5);
        printf("\n--- Sample %d ---\n", s);
        for(int i = 0; i < 5; i++) {
            if(getmlfqinfo(pids[i], &info) == 0) {
                char* class = (i < 3) ? "COMPUTE" : (i == 3 ? "INTERACT" : "BASELINE");
                printf("%d\t%d\t%s\t%d\t[%d, %d, %d, %d]\t%d\n",
                       s, pids[i], class, info.level,
                       info.ticks[0], info.ticks[1], info.ticks[2], info.ticks[3],
                       info.total_syscalls);
            }
        }
    }

    // Cleanup processes
    for(int i = 0; i < 5; i++) kill(pids[i]);
    while(wait(0) > 0);

    printf("Evaluation complete.\n");
    exit(0);
}
