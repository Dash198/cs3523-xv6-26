#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         100//(MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       60000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define USERSTACK    1     // user stack pages
#define MAX_USER_FRAMES 64 // maximum user frames in the frame table
#define MAX_SWAP_PAGES 1024 // max pages that can be brought into swap
#define SWAP_START  10000   // start of swap space on the disk
#define RAID_DISK_SIZE 10000  // Size of each disk in RAID
#define BLOCKS_PER_PAGE 4   // No. of disk blocks req for a page
