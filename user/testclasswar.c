#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NUM_PAGES 40

struct vmstats;

struct mlfqinfo {
  int level;            // current queue level
  int ticks[4];         // total ticks consumed at each level
  int times_scheduled;  // number of times the process has been scheduled
  int total_syscalls;   // total system calls made
};

void memory_hog(char *name, int is_vip) {
    char *mem[NUM_PAGES];
    int pid = getpid();
    struct vmstats vm_before, vm_after;
    struct mlfqinfo m_info;

    getvmstats(pid, &vm_before);

    // Allocate and write
    for(int i = 0; i < NUM_PAGES; i++){

      // --- PHASE 1: Establish MLFQ Status ---
          if (is_vip) {
              printf("[%s] Spamming syscalls to establish VIP status...\n", name);
              for(int j = 0; j < 80000; j++) getpid();
          } else {
              printf("[%s] Burning massive CPU to force demotion (This might take 5-10 seconds!)...\n", name);

              // The O(N^2) CPU Torture Chamber
              volatile long long sum = 0;
              for(long long j = 0; j < 50000; j++) {
                  for(long long k = 0; k < 10000; k++) {
                      sum += (j * k); // Multiplication is much slower than addition
                  }
              }
              printf("[%s] CPU Burn complete! Moving to allocation...\n", name);
          }

        mem[i] = malloc(PGSIZE);
        if(mem[i] == 0) exit(1);
        mem[i][0] = 'X';
    }

    // Read back to force swap-ins
    for(int i = 0; i < NUM_PAGES; i++){

        if (is_vip) {
            for(int j = 0; j < 1000; j++) getpid();
        }

        char c = mem[i][0];
        if (c != 'X') exit(1);
    }

    getvmstats(pid, &vm_after);

    // Fetch MLFQ stats to see exactly what happened!
    // (Ensure your syscall signature matches this: getmlfqinfo(pid, &struct))
    if(getmlfqinfo(pid, &m_info) < 0){
        printf("Failed to fetch MLFQ info for %s\n", name);
    }

    printf("\n>>> [%s] FINAL STATS <<<\n", name);
    printf("--- VM Telemetry ---\n");
    printf("Total Evictions: %d\n", vm_after.pages_evicted - vm_before.pages_evicted);
    printf("Total Swap-Ins:  %d\n", vm_after.pages_swapped_in - vm_before.pages_swapped_in);

    printf("--- MLFQ Telemetry ---\n");
    printf("Final Queue Level: %d\n", m_info.level);
    printf("Times Scheduled:   %d\n", m_info.times_scheduled);
    printf("Total Syscalls:    %d\n", m_info.total_syscalls);
    printf("Ticks per level:   [L0: %d, L1: %d, L2: %d, L3: %d]\n",
            m_info.ticks[0], m_info.ticks[1], m_info.ticks[2], m_info.ticks[3]);

    exit(0);
}

int main(int argc, char *argv[]) {
    printf("=== The Delta-System Class War ===\n\n");

    int pid = fork();

    if(pid < 0) {
        printf("Fork failed!\n");
        exit(1);
    }

    if(pid == 0) {
        // --- CHILD PROCESS (THE PEASANT) ---
        memory_hog("PEASANT", 0);
    }
    else {
        // --- PARENT PROCESS (THE VIP) ---
        memory_hog("VIP", 1);
        wait(0);
    }

    exit(0);
}
