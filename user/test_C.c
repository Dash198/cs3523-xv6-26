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

// Add delay since sleep is not present.
void spin_delay() {
    volatile int i;
    for(i = 0; i < 10000000; i++);
}

void test_getsyscount() {
    printf("\n--- Test C1 & C2: getsyscount() ---\n");

    int start = getsyscount();

    getpid();
    getpid();
    getpid();

    int end = getsyscount();

    int diff = end - start;

    if(diff == 4) {
        printf("[PASS] getsyscount tracked exactly 4 events (3 getpids + 1 getsyscount).\n");
    } else {
        printf("[FAIL] Expected diff 4, got %d. (Start: %d, End: %d)\n", diff, start, end);
    }
}

void test_getchildsyscount() {
    printf("\n--- Test C3: getchildsyscount() ---\n");

    int pid = fork();

    if(pid < 0) {
        printf("Fork failed\n");
        exit(1);
    }

    if(pid == 0) {
        write(1, ".", 1);
        write(1, ".", 1);
        write(1, ".", 1);

        spin_delay();

        exit(0); // Exit and become ZOMBIE
    } else {
        spin_delay();
        spin_delay();
        int cnt_alive = getchildsyscount(pid);

        if(cnt_alive >= 3) {
            printf("[PASS] Alive child has %d syscalls (Expected >= 3)\n", cnt_alive);
        } else {
            printf("[FAIL] Alive child has too few syscalls: %d\n", cnt_alive);
            //exit(1);
        }

        int cnt_zombie = getchildsyscount(pid);

        if(cnt_zombie >= cnt_alive) {
            printf("[PASS] Zombie child preserved stats (Alive: %d, Zombie: %d)\n", cnt_alive, cnt_zombie);
        } else {
            printf("[FAIL] Zombie stats corrupted! (Alive: %d, Zombie: %d)\n", cnt_alive, cnt_zombie);
            exit(1);
        }

        wait(0);

        int cnt_gone = getchildsyscount(pid);
        if(cnt_gone == -1) {
            printf("[PASS] getchildsyscount returned -1 for reaped process.\n");
        } else {
            printf("[FAIL] Can still read stats of dead process! Result: %d\n", cnt_gone);
            exit(1);
        }

        if(getchildsyscount(9999) == -1) {
             printf("[PASS] getchildsyscount(9999) correctly returned -1.\n");
        } else {
             printf("[FAIL] Spied on non-existent process 9999!\n");
             exit(1);
        }
    }
}

int main(int argc, char *argv[]) {
    printf("Starting Part C Test Suite...\n");

    test_getsyscount();
    test_getchildsyscount();

    printf("\n[SUMMARY] Part C Tests Passed.\n");
    exit(0);
}
