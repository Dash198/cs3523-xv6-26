#include "kernel/types.h"
#include "user/user.h"

int main() {
    int pid = fork();
    if(pid == 0){
        // A controlled hog that runs long enough to hit L3
        for(volatile long long i = 0; i < 2000000000LL; i++);
        exit(0);
    }

    struct mlfqinfo info;
    printf("Verifying Migration for PID %d...\n", pid);
    for(int i = 0; i < 10; i++){
        pause(10);
        if(getmlfqinfo(pid, &info) == 0){
            printf("Step %d: Level %d | Ticks [%d, %d, %d, %d]\n",
                i, info.level, info.ticks[0], info.ticks[1], info.ticks[2], info.ticks[3]);
        }
    }
    kill(pid);
    wait(0);
    printf("Test 1 Complete.\n");
    exit(0);
}
