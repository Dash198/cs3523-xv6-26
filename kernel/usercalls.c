#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64 sys_hello(void){
  printf("Hello from the kernel!\n");
  return 0;
}

uint64 sys_getpid2(void){
  return myproc() -> pid;
}

uint64 sys_getppid(void){
  struct proc *p = myproc();
  acquire(&p->lock);

  if(p->parent){
    int ppid = p->parent->pid;
    release(&p->lock);

    return ppid;
  }

  return -1;
}

uint64 sys_getnumchild(void){
  return get_children(myproc()->pid);
}

uint64 sys_getsyscount(void){
  return myproc()->syscall_count;
}

uint64 sys_getchildsyscount(void){
  int pid;

  argint(0, &pid);

  return get_child_syscall(pid);
}

uint64 sys_getlevel(void){
  return myproc()->priority_level;
}

uint64 sys_getmlfqinfo(void){
  int pid;
  argint(0, &pid);

  uint64 addr;
  argaddr(1, &addr);

  return getmlfqinfo(pid, addr);
}

uint64 sys_getvmstats(void){
  int pid;
  argint(0, &pid);

  uint64 addr;
  argaddr(1, &addr);

  return getvmstats(pid, addr);
}

uint64 sys_setdisksched(void){
  int mode;
  argint(0, &mode);

  disk_sched_policy = mode;
  return 0;
}

uint64 sys_setraidlevel(void){
  int level;
  argint(0, &level);

  raid_level = level;
  return 0;
}

uint64 sys_faildisk(void){
  int disk;
  argint(0, &disk);

  disk_failed = disk;
  return 0;
}
