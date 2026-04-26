# CS3523: Programming Assignment 4

by Devansh Tripathi (CS24BTECH11022)

---

## Problem Statement
This assignment required us to change the in-memory swap to a disk backed swap with RAID integration, adding in RAID levels 0, 1 and 5.

## Approach

The implementation was approached in three distinct phases, moving from the high-level Virtual Memory management down to the low-level hardware orchestration.

### Virtual Memory to Disk Bridge
The first step was transitioning xv6 from a RAM-backed swap (from PA3) to a persistent Disk-backed swap. 
* **Sector-Level Addressing:** Transition from indexing slots in a memory array to addressing blocks in the disk. 
* **The `SWAP_START` Boundary:** Blocks below `SWAP_START` were reserved for the native xv6 filesystem, while blocks above it were treated as a Virtual RAID Device. This ensured that RAID logic never corrupted the core OS files.

### Multi-Disk Driver Abstraction
Rather than treating the disk as a single entity, the driver was refactored to support a 4-Disk Array.
* **Parallel Queuing:** A Disk-per-Queue model was implemented. Each of the four disks was assigned a dedicated `disk_queue` structure. 
* **Atomic Submission:** To prevent race conditions in a multi-core environment, per-queue spinlocks were introduced. This allowed the kernel to enqueue requests for Disk 0 while simultaneously processing completion interrupts for Disk 2.


### Adaptive Scheduling and Redundancy
The final layer involved the logic of how and when requests are dispatched.
* **Redundancy Logic (RAID):** The RAID levels were implemented as a transformation layer. When `virtio_disk_rw` is called, the logical `blockno` is translated into Target Disk and Physical Sector based on the active RAID level (0, 1, or 5).
* **Heuristic Dispatching:** Both the FCFS and the SSTF algorithms are used to dispatch blocks, with either being priority-aware.
* **MLFQ Integration:** To bridge the gap between CPU scheduling and I/O, the process's MLFQ priority level was passed into the buffer metadata, allowing the disk driver to prioritize interactive tasks over CPU-bound background tasks.

### Segmentation of the Disk I/O Lifecycle
To handle the transition from a single-disk model to a complex RAID array, the I/O process was segmented into four distinct stages. This modularity was essential for implementing the Read-Modify-Write (RMW) cycle required by RAID 5.

* **Stage 1: Logical Mapping & Metadata Capture**:
    Upon a `virtio_disk_rw` call, the kernel captures the process's MLFQ priority and PID. This metadata is tagged onto the `struct buf`. The Stripe Index and Parity Rotation were calculated in case it was RAID 5. By calculating these values upfront but keeping the `b->blockno` immutable, the buffer-cache aliasing bugs were avoided that would otherwise occur if blocks mid-flight were renamed.

* **Stage 2: RAID Logic**: 
    * For RAID 0, it is a simple pass-through. 
    * For RAID 1, it initiates two sequential submissions. 
    * For RAID 5, this stage handles the complexity of parity. If a write is requested, the segment initiates a sub-transaction to read the old data and old parity before computing the new parity and dispatching the final writes.

* **Stage 3: Buffered Queuing & Scheduling**: 
    Instead of sending requests directly to the hardware, they are placed in one of four Per-Disk Queues. Each queue acts as a waiting room where the scheduling algorithm can reorder requests based on the physical position of the disk head and the priority of the originating process.

* **Stage 4: Hardware Dispatch & Interrupt Recovery**: 
    The `dispatch_disk_request` function translates the scheduled buffer into a physical sector address. Once the VirtIO hardware signals completion via an interrupt, the `virtio_disk_intr` handler wakes the sleeping process. In RAID 5, if this stage reports a hardware error, the segment pivots to triggering an XOR reconstruction using the remaining disks.

### System Call Extensions
Three primary system calls were added to bridge the gap between the kernel's RAID/Scheduling logic and user-space telemetry.

| System Call | Purpose | Impact on Testing |
| :--- | :--- | :--- |
| `setdisksched(int policy)` | Toggles between **FCFS (0)** and **SSTF (1)**. | Allowed for direct back-to-back performance benchmarking without reboots. |
| `setraidlevel(int level)` | Dynamically switches the active RAID level (0, 1, or 5). | Enabled the tests to verify data integrity across different redundancy models in a single run. |
| `getvmstats(int pid, struct vmstats*)` | Provides high-fidelity telemetry including Disk Reads/Writes and Average Latency. | Transformed disk accounting to a per-process resource tracking tool. |

---

## Design Decisions and Assumptions

### Integer-Only Latency Math
* **Decision:** All latency and averaging calculations are performed using integer math.
* **Assumption:** Since the RISC-V kernel does not natively save/restore floating-point registers during traps, using `float` would have risked register corruption. We assumed that ticks were a granular enough unit for average latency reporting.

### Frame Level Atomicity
* **Decision**: To handle the high-latency nature of RAID 5 I/O, we introduced an `is_swapping` flag within each frame_entry. This serves as a synchronization primitive during page eviction. When `evict_page()` selects a victim, it marks the frame as swapping before releasing the `ft_lock` to perform the disk write. This ensures that the Clock algorithm and other memory-management functions (like `remove_frame`) bypass frames currently involved in active I/O, preventing data corruption and race conditions during the slow disk swapping process.

---

## Experimental Results

64 pages were allocated to memory for testing.

### Test 1: RAID 0 & 1 Integrity and Write Amplification (`test_integrity`)
**Objective:** Verify data persistence and observe how RAID levels multiply disk traffic.
* **Observation:** The system successfully swapped 70 pages. 
* **Data Integrity:** Every integer written to the 70 pages was verified correctly after being swapped back in.
* **Results:**
    * **RAID 0:** Observed 256 writes for 52 base blocks. The overhead is attributed to xv6 filesystem metadata synchronization.
    * **RAID 1:** Observed 384 writes. This confirmed the mirroring logic was active, as the write count increased significantly to account for the duplicated data across two disks.
    * **Disk Efficiency:** RAID 1 reported 0 Reads during verification, confirming that the `bcache` successfully held the swapped-in data in memory.

### Test 2: Disk Scheduler Performance (FCFS vs. SSTF) (`test_perf`)
**Objective:** Measure the impact of the Shortest Seek Time First (SSTF) algorithm against the default FCFS using a "Scatter Read" workload.
* **Pattern:** An outside-in read pattern ($[0, 69, 1, 68 \dots]$) was used to maximize disk head travel.
* **Results:**
    * **FCFS:** 34 ticks average latency.
    * **SSTF:** 12 ticks average latency.
* **Conclusion:** SSTF provided a $64.7\%$ reduction in latency. This proves the scheduler effectively reordered the scattered requests to minimize seek distance.

### Multi-Eviction Swap Correctness (RAID 5) (`test_correct`)
**Objective:** Ensure the RAID 5 system holds up when pages are repeatedly evicted and swapped back in.
* **Pattern:** 100 pages written sequentially and read back in reverse order to force maximum swap-in pressure.
* **Results:**
    * **Data Integrity:** No errors
    * **Metrics:** 112 page faults and 54 swap-outs were handled flawlessly. 
* **Conclusion:** In a single-process environment, the RAID 5 parity rotation and Read-Modify-Write (RMW) logic are mathematically correct.

### Test 4: Integration Stress Test (RAID 5) (`test_raid5`)
**Objective:** Push the RAID 5 implementation to its limits using 4 concurrent processes performing heavy I/O.
* **Observation:** The kernel remained stable throughout the test (no panics or deadlocks), and the SSTF scheduler successfully managed the high-frequency interrupt load.
* **Data Integrity:** Mixed results.
    * Some successfully verified all pages.
    * Some reported FATAL ERROR (Critical Integrity Failure).
    * Some triggered a `scause 0xf` (Store Page Fault).
* **Analysis:** The failures demonstrate that while the driver is stable, concurrent writes to the same RAID 5 stripe result in non-deterministic data corruption. The `scause 0xf` indicates that this corruption extended into the process's heap metadata, causing a memory fault.

### Test 5: Process Exit During Active Swap (`test_exit_swap`)
**Objective:** Verify safe handling of orphaned I/O and prevention of swap leaks.
* **Result:** Parent reaped children mid-swap over 3 rounds without kernel panic.
* **Validation:** Subsequent fresh allocations succeeded.
* **Analysis:** Correctly identifies terminated processes and prevents swap-table corruption or resource leaks.

Apart from this, the test cases used for PA3 also work correctly.

---

## LLM Usage Declaration

* **Tool:** Google Gemini
* **Extent of Usage:** Assisted in studying code, helped in devising the architecture (double frame eviction, synchronising multiple disk queues, preventing queue starvation), debugging some kernel traps and panics, designing test cases and formatting this report.
