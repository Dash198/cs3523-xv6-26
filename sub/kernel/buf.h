struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext;  // Buffer linked list for disk queue
  int is_write;       // R or W
  int priority;       // Priority of the corresponding process
  uchar data[BSIZE];
  int queue_id;   // Target disk
  int status;     // disk stats (active or down?)
  int pid;      // process pid
};
