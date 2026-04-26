#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void assert(int condition, char *msg) {
    if(condition) {
        printf("[PASS] %s\n", msg);
    } else {
        printf("[FAIL] %s\n", msg);
        exit(1);
    }
}

void test_getppid() {
    printf("\n--- Test B1: getppid() ---\n");

    int parent_pid = getpid();
    int pid = fork();

    if(pid < 0) {
        printf("Fork failed\n");
        exit(1);
    }

    if(pid == 0) {
        int my_ppid = getppid();
        if(my_ppid == parent_pid) {
            printf("[PASS] Child correctly identified Parent (Parent PID: %d)\n", parent_pid);
            exit(0);
        } else {
            printf("[FAIL] Child thinks parent is %d, but it should be %d\n", my_ppid, parent_pid);
            exit(1);
        }
    } else {
        wait(0);
        printf("[PASS] Parent received success from child.\n");
    }
}

void test_getnumchild() {
    printf("\n--- Test B2: getnumchild() ---\n");

    int initial_children = getnumchild();
    if(initial_children != 0) {
        printf("[WARN] Starting with %d children (Expected 0, but maybe previous tests leaked?)\n", initial_children);
    }

    printf("1. Forking 3 children...\n");
    int pid1 = fork();
    if(pid1 == 0) {
        // Delay for the parent to detect children (cannot access sleep)
        volatile int i;
        for(i = 0; i < 100000000; i++) {
        }
        exit(0);
    }

    int pid2 = fork();
    if(pid2 == 0) {
        volatile int i;
        for(i = 0; i < 100000000; i++) {
        }
        exit(0);
    }

    int pid3 = fork();
    if(pid3 == 0) {
        volatile int i;
        for(i = 0; i < 100000000; i++) {
        }
        exit(0);
    }

    int count = getnumchild();
    if(count == 3) {
        printf("[PASS] getnumchild() counts 3 active children.\n");
    } else {
        printf("[FAIL] getnumchild() counted %d (Expected 3)\n", count);
    }

    printf("2. Killing children and checking count...\n");
    kill(pid1); wait(0);
    kill(pid2); wait(0);
    kill(pid3); wait(0);

    count = getnumchild();
    if(count == 0) {
        printf("[PASS] getnumchild() counts 0 after cleanup.\n");
    } else {
        printf("[FAIL] getnumchild() counted %d after wait (Expected 0)\n", count);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    printf("Starting Part B Test Suite...\n");

    test_getppid();
    test_getnumchild();

    printf("\n[SUMMARY] Part B Tests Passed.\n");
    exit(0);
}
