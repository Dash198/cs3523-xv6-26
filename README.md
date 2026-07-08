# Advanced xv6: MLFQ, Priority-Aware CLOCK, and RAID-Backed Swap
An enhanced version of the MIT xv6 operating system featuring a multi-level feedback queue (MLFQ) scheduler, a priority-aware CLOCK page replacement algorithm, and a fully asynchronous, persistent RAID-backed swap system (supporting RAID 0, 1, and 5). 
This project demonstrates advanced operating system concepts, focusing on deep kernel modifications, hardware-software synchronization, and complex concurrency management.
## Key Features
### 1. Process Priorities & MLFQ Scheduling
* **Multi-Level Feedback Queue:** Implemented a priority-based scheduler where processes dynamically shift between priority levels based on their CPU usage. CPU-bound tasks are demoted to allow interactive/I/O-bound tasks to remain responsive.
* **Priority Boosting:** Prevents starvation by periodically boosting all processes to the highest priority queue, ensuring fair resource distribution under heavy load.
* **Syscall Integration:** Added `set_priority` system calls to allow user-space programs to manually adjust their execution priorities.
### 2. Priority-Aware CLOCK Page Replacement
* **Hardware Page Tracking:** Utilizes RISC-V PTE Access/Reference bits to track page usage, implementing the CLOCK algorithm to efficiently locate eviction candidates.
* **Priority-Aware Eviction:** Integrates with the MLFQ scheduler by evaluating a process's priority during page eviction. The system intelligently defers evicting pages belonging to high-priority interactive processes, deferring the disk I/O penalty to lower-priority background tasks.
### 3. Persistent Disk Scheduling & RAID-Backed Swap
* **Asynchronous I/O Queue:** Decoupled memory eviction from VirtIO hardware writes. Processes are put to sleep while a lock-free First-Come, First-Served (FCFS) or Shortest Seek Time First (SSTF) dispatcher handles disk I/O asynchronously.
* **Dynamic Disk Scheduling:** Configurable scheduling policies (FCFS vs. SSTF). SSTF includes priority-awareness, using physical seek distance and process priority to optimize disk head movement and latency.
* **RAID Simulation:** Abstracted the QEMU VirtIO device into a 4-disk virtual RAID array:
    * **RAID 0 (Striping):** Perfect distribution of continuous memory pages across disks for maximum throughput.
    * **RAID 1 (Mirroring):** Redundant block writes ensuring 100% data integrity with failover support.
    * **RAID 5 (Distributed Parity):** Complex Read-Modify-Write (RMW) cycle using XOR bitwise math to distribute parity across disks, surviving single-disk failures while optimizing capacity.
## Concurrency & Kernel Stability
This project successfully mitigates several complex, low-level concurrency hazards common in asynchronous kernel environments:
* **The Double-Eviction Race Condition:** Resolved Time-of-Check to Time-of-Use (TOCTOU) bugs in the frame allocator by implementing instantaneous atomic "claiming" of victim frames before initiating asynchronous disk I/O.
* **Buffer Cache Poisoning:** Solved cache aliasing bugs by implementing "Late Address Translation", ensuring logical block IDs remain immutable in the `bcache` while mathematical RAID offsets are applied strictly at the hardware dispatch layer.
* **Post-Sleep Reality Checks:** Implemented safety nets to prevent null-pointer dereferences when a process is killed while sleeping on a disk I/O request, ensuring safe teardown of "ghost" memory.
## Building and Testing
To compile and boot the customized xv6 kernel in QEMU:
```bash
make qemu
```
