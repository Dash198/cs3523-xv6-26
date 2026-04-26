#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void spin_delay(int loops) {
    volatile int i;
    for(i = 0; i < loops * 1000000; i++);
}

void assert(int condition, char *msg) {
    if(condition) {
        printf("[PASS] %s\n", msg);
    } else {
        printf("[FAIL] %s\n", msg);
    }
}

void test_bad_inputs() {
    printf("\n--- Edge Case 1: Bad Inputs ---\n");

    int res1 = getchildsyscount(-1);
    if(res1 == -1) printf("[PASS] Negative PID returns -1\n");
    else printf("[FAIL] Negative PID returned %d\n", res1);

    int res2 = getchildsyscount(9999);
    if(res2 == -1) printf("[PASS] Huge PID returns -1\n");
    else printf("[FAIL] Huge PID returned %d\n", res2);

    int res3 = getchildsyscount(getpid());
    if(res3 == -1) printf("[PASS] Self-PID returns -1 (Not my child)\n");
    else printf("[FAIL] Self-PID returned %d\n", res3);
}

void test_orphan() {
    printf("\n--- Edge Case 2: The Orphan ---\n");

    int pid = fork();
    if(pid == 0) {
        int p2 = fork();
        if(p2 == 0) {
            spin_delay(50); // Wait for middle parent to die

            int pp = getppid();
            if(pp == 1) {
                 printf("[PASS] Orphan adopted by Init (PPID: 1)\n");
            } else {
                 printf("[FAIL] Orphan has wrong parent! PPID: %d\n", pp);
            }
            exit(0);
        }
        exit(0); // Middle parent dies immediately
    }

    // Original parent waits for middle parent
    wait(0);

    // Grandchild is running in background, let it finish printing
    spin_delay(100);
}

void test_exec_persistence() {
    printf("\n--- Edge Case 3: Exec Persistence ---\n");

    int pid = fork();
    if(pid == 0) {
        getpid(); getpid(); getpid();

        // Replace self with 'echo'
        char *argv[] = { "echo", "   [Exec'd Child Speaking]", 0 };
        exec("echo", argv);

        printf("Exec failed!\n");
        exit(1);
    } else {
        // --- PARENT ---
        spin_delay(50); // Let child exec and print

        int cnt = getchildsyscount(pid);

        // Count should include:
        // 1. Pre-exec calls (3 getpids)
        // 2. The exec call itself
        // 3. Post-exec calls (echo does write, exit, etc.)
        // Total should be > 4. If it reset to 0, it would be very small.

        if(cnt > 4) {
             printf("[PASS] Counter persisted across exec() (Count: %d)\n", cnt);
        } else {
             printf("[FAIL] Counter too low! (Count: %d)\n", cnt);
        }

        wait(0);
    }
}

void test_grandchild() {
    printf("\n--- Edge Case 4: The Grandchild Spy ---\n");

    int pipe_fd[2];
    pipe(pipe_fd); // Use a pipe to pass the Grandchild's PID back to Grandpa

    int child = fork();
    if(child == 0) {
        // --- CHILD (Middle) ---
        int grandchild = fork();
        if(grandchild == 0) {
            // --- GRANDCHILD ---
            // Just spin to stay alive so we can be tested
            spin_delay(50);
            exit(0);
        }

        // Child sends Grandchild's PID to Grandpa
        write(pipe_fd[1], &grandchild, sizeof(grandchild));
        close(pipe_fd[1]);

        // Wait for grandchild to finish
        wait(0);
        exit(0);
    }

    // --- GRANDPA (You) ---
    close(pipe_fd[1]); // Close write end

    int grandchild_pid = 0;
    read(pipe_fd[0], &grandchild_pid, sizeof(grandchild_pid));
    close(pipe_fd[0]);

    // Now try to spy on the grandchild while it's spinning
    // Expected: -1 (Not my direct child!)
    int res = getchildsyscount(grandchild_pid);

    if(res == -1) {
        printf("[PASS] Correctly blocked access to Grandchild PID %d\n", grandchild_pid);
    } else {
        printf("[FAIL] Spied on Grandchild! (Returned: %d)\n", res);
    }

    wait(0); // Wait for Child
}

int main(void) {
    printf("Starting Edge Case Gauntlet (No Sleep Mode)...\n");

    test_bad_inputs();
    test_orphan();
    test_exec_persistence();
    test_grandchild();

    printf("\n[SUMMARY] Edge Cases Completed.\n");
    exit(0);
}
