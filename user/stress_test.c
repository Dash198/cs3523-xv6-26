#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define N_loop 1000
#define MAX_PROCS 60

void assert(int condition, char *msg) {
    if(condition) {
    } else {
        printf("[FAIL] %s\n", msg);
        exit(1);
    }
}

void test_concurrency() {
    printf("\n--- Stress 1: The Hammer (Concurrency) ---\n");
    int pid = fork();

    if(pid == 0) {
        // Child: Generates syscalls as fast as possible
        for(int i = 0; i < N_loop; i++) {
            getpid(); // Cheap syscall
        }
        exit(0);
    } else {
        // Parent: Reads the count as fast as possible
        int prev_count = -1;
        int fails = 0;

        // We loop slightly more than the child to ensure we catch the finish.
        for(int k = 0; k < N_loop * 2; k++) {
            int count = getchildsyscount(pid);

            // If we somehow missed the child (unlikely), stop
            if(count == -1) break;

            // CRITICAL CHECK: Monotonicity
            if(count < prev_count) {
                printf("[FAIL] Race condition! Count went backwards: %d -> %d\n", prev_count, count);
                fails++;
            }
            prev_count = count;

            // Optimization: If the count stops changing (Zombie state reached), we can stop early
            // But for stress testing, it's fine to keep checking the static zombie value.
        }

        wait(0);

        if(fails == 0) {
            printf("[PASS] Syscall count was strictly monotonic under load.\n");
        }
    }
}

void test_fork_bomb() {
    printf("\n--- Stress 2: The Fork Bomb (Table Exhaustion) ---\n");

    int children[MAX_PROCS];
    int created = 0;

    printf("Spawning processes until failure...\n");

    for(int i = 0; i < MAX_PROCS; i++) {
        int pid = fork();
        if(pid < 0) {
            printf("Process table full at %d children. (Expected behavior)\n", i);
            break;
        }
        if(pid == 0) {
            // Child: Just sleep and exit
            // We use a busy loop because we assume sleep() is still missing
            volatile int x = 0;
            for(int j=0; j<1000000; j++) x++;
            exit(0);
        }
        children[i] = pid;
        created++;
    }

    int num = getnumchild();
    if(num == created) {
        printf("[PASS] getnumchild correctly tracked %d processes under load.\n", num);
    } else {
        printf("[FAIL] getnumchild lost count! Created: %d, Counted: %d\n", created, num);
    }

    if(created > 0) {
        int last_pid = children[created-1];
        if(getchildsyscount(last_pid) > 0) {
            printf("[PASS] Can read stats of child #%d (PID %d) at end of table.\n", created, last_pid);
        } else {
            printf("[FAIL] Failed to read stats of child #%d\n", created);
        }
    }

    printf("Reaping %d children...\n", created);
    for(int i = 0; i < created; i++) {
        wait(0);
    }
}

void test_sibling_spy() {
    printf("\n--- Stress 3: Sibling Spy (Unauthorized Access) ---\n");

    int pipe_fd[2];
    pipe(pipe_fd);

    int pid1 = fork();
    if(pid1 == 0) {
        // Child 1 (The Target)
        volatile int x = 0;
        for(int j=0; j<10000000; j++) x++;
        exit(0);
    }

    int pid2 = fork();
    if(pid2 == 0) {
        // Child 2 (The Spy)
        // Try to read Child 1's stats
        // Since Child 1 is NOT Child 2's child, this MUST fail.
        int res = getchildsyscount(pid1);
        if(res == -1) {
            write(pipe_fd[1], "P", 1); // Pass
        } else {
            write(pipe_fd[1], "F", 1); // Fail
        }
        exit(0);
    }

    wait(0);
    wait(0);

    char result;
    read(pipe_fd[0], &result, 1);

    if(result == 'P') {
        printf("[PASS] Sibling could not spy on sibling.\n");
    } else {
        printf("[FAIL] Security Breach! Sibling read another sibling's stats.\n");
    }
}

int main(void) {
    printf("Starting Security Stress Test...\n");

    test_concurrency();
    test_fork_bomb();
    test_sibling_spy();

    printf("\n[SUMMARY] System Survived Stress Testing.\n");
    exit(0);
}
