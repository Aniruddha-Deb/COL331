#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// Fetch the int at addr from the current process.
int
fetchint(uint addr, int *ip)
{
  struct proc *curproc = myproc();

  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
int
fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){
    if(*s == 0)
      return s - *pp;
  }
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
int
argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();
 
  if(argint(n, &i) < 0)
    return -1;
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  *pp = (char*)i;
  return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
int
argstr(int n, char **pp)
{
  int addr;
  if(argint(n, &addr) < 0)
    return -1;
  return fetchstr(addr, pp);
}

extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);
extern int sys_print_count(void);
extern int sys_toggle(void);
extern int sys_add(void);

static int trace_calls = TRACE_OFF;

static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_print_count]  sys_print_count,
[SYS_toggle]  sys_toggle,
[SYS_add] sys_add
};

static int n_syscalls = NELEM(syscalls);
static int call_count[NELEM(syscalls)] = {};
static int syscall_alph_map[] = {
[SYS_add] = 0,
[SYS_chdir] = 1,
[SYS_close] = 2,
[SYS_dup] = 3,
[SYS_exec] = 4,
[SYS_exit] = 5,
[SYS_fork] = 6,
[SYS_fstat] = 7,
[SYS_getpid] = 8,
[SYS_kill] = 9,
[SYS_link] = 10,
[SYS_mkdir] = 11,
[SYS_mknod] = 12,
[SYS_open] = 13,
[SYS_pipe] = 14,
[SYS_print_count] = 15,
[SYS_read] = 16,
[SYS_sbrk] = 17,
[SYS_sleep] = 18,
[SYS_toggle] = 19,
[SYS_unlink] = 20,
[SYS_uptime] = 21,
[SYS_wait] = 22,
[SYS_write] = 23
};

static char* syscall_name_map[] = {
"sys_add",
"sys_chdir",
"sys_close",
"sys_dup",
"sys_exec",
"sys_exit",
"sys_fork",
"sys_fstat",
"sys_getpid",
"sys_kill",
"sys_link",
"sys_mkdir",
"sys_mknod",
"sys_open",
"sys_pipe",
"sys_print_count",
"sys_read",
"sys_sbrk",
"sys_sleep",
"sys_toggle",
"sys_unlink",
"sys_uptime",
"sys_wait",
"sys_write"
};

void
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax;
  if(num > 0 && num < n_syscalls && syscalls[num]) {
    if(trace_calls) {
      call_count[syscall_alph_map[num]] += 1;
    }
    curproc->tf->eax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}

int
toggle(void) {
  int i;
  if (trace_calls == TRACE_ON) trace_calls = TRACE_OFF;
  else {
    trace_calls = TRACE_ON;
    for (i=0; i<n_syscalls; i++) {
      call_count[i] = 0;
    }
  }
  return 0;
}

int
print_count(void)
{
  int i;
  // note that syscalls are 1-indexed and not 0-indexed, so we do n_syscalls-1
  for (i=0; i<n_syscalls-1; i++) {
    cprintf("%s %d\n", syscall_name_map[i], call_count[i]);
  }
  return 0;
}
