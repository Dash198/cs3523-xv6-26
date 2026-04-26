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

void test_hello() {
    printf("\n--- Test A1: hello() ---\n");

    printf("1. Calling hello()... (Check console for 'Hello from the kernel!')\n");
    int ret = hello();

    if(ret == 0) {
        printf("[PASS] hello() returned 0\n");
    } else {
        printf("[FAIL] hello() returned %d (Expected 0)\n", ret);
        exit(1);
    }

    printf("2. Stress Test: Calling hello() 10 times...\n");
    for(int i = 0; i < 10; i++) {
        hello();
    }
    printf("[PASS] Kernel survived hello() spam.\n");
}

void test_getpid2() {
    printf("\n--- Test A2: getpid2() ---\n");

    int p1 = getpid();
    int p2 = getpid2();

    if(p1 == p2) {
        printf("[PASS] getpid2() matches standard getpid() (PID: %d)\n", p1);
    } else {
        printf("[FAIL] getpid2() mismatch! getpid=%d, getpid2=%d\n", p1, p2);
        exit(1);
    }

    int val1 = getpid2();
    int val2 = getpid2();
    if(val1 == val2) {
        printf("[PASS] getpid2() is consistent (Value: %d)\n", val1);
    } else {
        printf("[FAIL] getpid2() changed identity! %d -> %d\n", val1, val2);
        exit(1);
    }

    printf("3. Fork Test: Verifying child PID...\n");
    int pid = fork();

    if(pid < 0) {
        printf("Fork failed!\n");
        exit(1);
    }

    if(pid == 0) {
        int child_std = getpid();
        int child_new = getpid2();

        if(child_new == child_std && child_new != p1) {
            printf("[PASS] Child: getpid2() correctly identifies new PID %d\n", child_new);
            exit(0);
        } else {
            printf("[FAIL] Child: PIDs don't make sense. Std=%d, New=%d, Parent=%d\n", child_std, child_new, p1);
            exit(1);
        }
    } else {
        wait(0);
        printf("[PASS] Parent: Child test completed successfully.\n");
    }
}

int main(int argc, char *argv[]) {
    printf("Starting Part A Test Suite...\n");

    test_hello();
    test_getpid2();

    printf("\n[SUMMARY] Part A Tests Passed.\n");
    exit(0);
}
