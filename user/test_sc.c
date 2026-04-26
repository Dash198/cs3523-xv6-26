#include "kernel/types.h"
#include "user/user.h"

void workload(int is_interactive) {
    for (int i = 0; i < 100; i++) {
        for (volatile int j = 0; j < 10000000; j++); // Compute
        if (is_interactive) {
            for (int k = 0; k < 500; k++) getpid(); // Syscall burst
        }
    }
    exit(0);
}

int main() {
    int h_pid = fork();
    if(h_pid == 0) workload(0);
    int s_pid = fork();
    if(s_pid == 0) workload(1);

    struct mlfqinfo hi, si;
    for(int i = 0; i < 15; i++) {
        pause(20);
        getmlfqinfo(h_pid, &hi);
        getmlfqinfo(s_pid, &si);
        printf("HOG: L=%d, SC=%d | SYS: L=%d, SC=%d\n",
               hi.level, hi.total_syscalls, si.level, si.total_syscalls);
    }
    kill(h_pid); kill(s_pid);
    wait(0); wait(0);
    exit(0);
}
