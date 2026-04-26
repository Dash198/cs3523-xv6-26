#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

struct vmstats;

int main(int argc, char *argv[]) {
    printf("=== Edge Case & Security Testing ===\n\n");

    struct vmstats st;
    int pid = getpid();

    // EDGE CASE 1: The Ghost Process
    printf("Test 1: Fetching stats for PID 9999...\n");
    if(getvmstats(9999, &st) < 0) {
        printf("PASS: Kernel safely rejected invalid PID.\n");
    } else {
        printf("FAIL: Kernel returned success for a non-existent PID!\n");
    }

    // EDGE CASE 2: The Null Pointer Trap
    // TAs love passing 0x0 to see if your kernel blindly writes to it
    // and triggers a page fault panic inside kernel space.
    printf("\nTest 2: Passing Null Pointer to getvmstats...\n");
    if(getvmstats(pid, (struct vmstats *)0x0) < 0) {
        printf("PASS: Kernel safely rejected bad memory address.\n");
    } else {
        printf("FAIL: Kernel accepted bad address! (Did you use copyout?)\n");
    }

    // EDGE CASE 3: The Out-Of-Bounds Trap
    printf("\nTest 3: Passing wildly out-of-bounds address...\n");
    if(getvmstats(pid, (struct vmstats *)0xffffffffffffffff) < 0) {
        printf("PASS: Kernel safely rejected kernel-space address.\n");
    } else {
        printf("FAIL: Kernel allowed user to write to kernel space!\n");
    }

    exit(0);
}
