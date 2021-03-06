#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

volatile int num_stride;
volatile int total_share;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct {
  struct proc *proc;
  int pass;
  int stride;
} stride_list[NPROC];

static struct proc *initproc;

int nextpid = 1;
uint next_tgid = 1; // next thread group id, used for bitwise operations
volatile int multithreading;

extern void forkret(void);
extern void trapret(void);
static void wakeup1(void *chan);

void
ret(void)
{
  asm volatile("ret");
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Getting maximum level of RUNNABLEs in mlfq
int
maxlev(void) {
  struct proc *p;
  int max = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->is_thread == 1) continue;
    if(p->state == RUNNABLE || (p->state == SLEEPING && p->num_thread > 0)) {
      if(p->mlfqlev > max) max = p->mlfqlev;
    }
  }
  return max;
}

// Priority boost
void
boost(void) {
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE ||
	p->state == RUNNING ||
	p->state == SLEEPING) {
      if(p->is_thread == 1) continue;
      if(p->mlfqlev != -1) p->mlfqlev = 2;
      //p->allotment = 50000000; FOR MLFQ + STRIDE
      p->allotment = 20 * TICKSIZE;
    }
  }
}

// Greatest Common Divisor Function
int
gcd(int a, int b) {
  if(a == 0 || b == 0)
    panic("Zero can't be GCDed");
  if(a == b) return a;
  if(a > b) return gcd(a-b, b);
  return gcd(a, b-a);
}

// Set stride - stride is multiple of all others except self
// RETURNS Least Common Multiple
void
set_stride(void) {
  struct proc *p;
  int i;

  int gcd_temp;
  int lcm_temp;
  int LCM; // The LCM
  total_share = 0;
  num_stride = 0;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    //stride_list[num_stride].pass = 0;
    if(p->is_stride) { // == 1?
      p->mlfqlev = -1;
      total_share += p->share;
      stride_list[num_stride].proc = p;
      num_stride++;
    }
  }

  gcd_temp = 100 - total_share;
  lcm_temp = 1;
  LCM = 100 - total_share;

  //cprintf("num: %d total_share: %d\n", num_stride, total_share);

  // Actual stride setting
  if(num_stride > 0) {
    // calculate gcd
    for(i = 0; i < num_stride; i++) {
      gcd_temp = gcd(gcd_temp, stride_list[i].proc->share);
    }
    for(i = 0; i < num_stride; i++) {
      stride_list[i].pass = stride_list[i].proc->share/gcd_temp;
      lcm_temp *= stride_list[i].pass;
    }

    LCM = lcm_temp * (100 - total_share);

    // SET STRIDE
    for(i = 0; i < num_stride; i++) {
      stride_list[i].stride = LCM / stride_list[i].proc->share;
      //cprintf("[%d]: %d ", stride_list[i].proc->share, stride_list[i].stride);
    }
    //cprintf("\n");
  }

  // Put all MLFQ in one Stride process;
  stride_list[num_stride].proc = 0;
  stride_list[num_stride].pass = 0;
  stride_list[num_stride].stride = LCM / (100 - total_share); // Sharesize of "one Stride process"


} 

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->mlfqlev = 2;
  p->allotment = 20 * TICKSIZE; // Also allotment for highest priority
  
  p->is_stride   = 0; // Cannot be stride process if newly forked
  p->share       = 0;

  p->is_thread = 0;
  p->num_thread = 0;
  p->num_sleeping_thread = 0;
  p->tgid = 0;
  
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  p->mlfqlev = 2;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
	pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
	if(p->is_stride) {
	  p->is_stride = 0;
	  set_stride();
	}

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p, *q;
  struct cpu *c = mycpu();
  int procrun;
  int passthru;
  int stride_index;
  int stampin, stampout;
  uint empty_mlfq; // when in stride scheduling, if there is no mlfq's to run
  //uint active_tgid;
  //uint thread_ticks;

  local_ticks = 5; // ???
  c->proc = 0;

  // Scheduler booting
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNING || p->state == RUNNABLE) {
      p->mlfqlev = 2;
      //p->allotment = 50000000; FOR MLFQ + STRIDe
      p->allotment = 20 * TICKSIZE;
    }
    else {
      p->mlfqlev = -2; // -1 for stride
      p->allotment = 0; // 0 time for nonexisting process
    }

    p->is_stride = 0;
    p->share = 0;
  }

  // No one except GOD can set Stride process
  // before even the scheduler has run
  num_stride = 0;
  set_stride();
  
  for(;;){
    // Enable interrupts on this processor.
    // IF pass through gets heavy, move this
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    // passes through number of stride lists
    passthru = stride_list[0].pass;
    stride_index = 0;
    if(num_stride > 0) {
      for(int i = 0; i <= num_stride; i++) {
        if(stride_list[i].pass <= passthru) {
	  passthru = stride_list[i].pass;
	  stride_index = i;
        }
      }

      if(passthru > 0) {
	if(passthru == stride_list[0].pass) {
	  for(int i = 0; i <= num_stride; i++)
	    stride_list[i].pass = 0;
	}
      }
    }



    if(stride_index == num_stride) { // Last one is MLFQ SET
      // MLFQ PART
      if(num_stride > 0) 
        empty_mlfq = ticks;
      
      // the loop
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        
	// inside
	if(p->state != RUNNABLE) {
	  if(p == &ptable.proc[NPROC]){
	    if(num_stride > 0)
	      while(ticks == empty_mlfq);
	  }
          if(p->num_thread <= 0)
	    continue;
	}

	if(p->is_thread == 1)
	  continue;

        if(p->mlfqlev != maxlev())
	  continue;

	if(num_stride == 0) {
	  switch(p->mlfqlev) {
	    case 2:
	      local_ticks = 5;
	      break;
	    case 1:
	      local_ticks = 10;
	      break;
	    case 0:
	      local_ticks = 20;
	      break;
	    default:
	      break;
	  }
	} else {
	  local_ticks = 5;
	}
        
	stampin = stamp();

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
	
	switchuvm(p);
	
	// multithreading scheduler ///////
	///////////////////////////////////
	if(p->num_thread > 0) {
	  multithreading = 0;
	  for(;;) {
	    for(q = ptable.proc; q < &ptable.proc[NPROC]; q++) {
	      if(p->num_thread == p->num_sleeping_thread) {
		goto opt_out;
	      }
	      if(multithreading == 0) {
		q = p->prev_thread + 1; // set first
		multithreading = 1;
	      }
	      if((q->state != RUNNABLE) || (q >= &ptable.proc[NPROC]) || (q->tgid != p->tgid))
		continue;
	      c->proc = q;
	      switchuvm_t(q);
	      q->state = RUNNING;
	      swtch(&(c->scheduler), q->context);
	      if(multithreading == 0)
		goto opt_out;
	    }
	  }
opt_out:
	  p->prev_thread = q;
	  multithreading = 0;
	} else {
	  p->state = RUNNING;
	  swtch(&(c->scheduler), p->context);
	}
	///////////////////////////////////

	switchkvm();

        stampout = stamp();
        procrun = (stampout - stampin)/2;
	
	if(p->mlfqlev != 0) p->allotment -= procrun; // No need to deal with allotment in level 0
	if(p->allotment < 0 && p->mlfqlev == 2) {
	  p->mlfqlev = 1;
	  //p->allotment = 100000000; FOR MLFQ + STRIDE
	  p->allotment = 40 * TICKSIZE;
        }
        if(p->allotment < 0 && p->mlfqlev == 1) {
	  p->mlfqlev = 0;
	  p->allotment = 0; // An Infinity
        }

        // ULTIMATE DEBUGGER
        // cprintf("[IN =%d OUT =%d, ELAPSED = %d, LEFT = %d, LEVEL = %d]\n", p->stampin, p->stampout, procrun, p->allotment, p->mlfqlev);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;

        // NEW STRIDE PROCESS
        // NOT THAT BEAUTIFUL
        // COULD POSSIBLY RESULT ERROR
        if(p->is_stride) break;

      } // ptable for loop

    } else { // Stride process begins
      // procrun = 50000000; // Stride is for 1 tick by default
      // procrun = 10000000; FOR MLFQ + STRIDE
      // lapic[0x0380/4] = procrun;
      if(local_ticks <= 0) local_ticks = 5;
      p = stride_list[stride_index].proc;

      // don't use continue here
      if(p->state == RUNNABLE) {
        c->proc = p;
	switchuvm(p);
	p->state = RUNNING;
	swtch(&(c->scheduler), p->context);
	switchkvm();
	c->proc = 0;
      }
    }

    __sync_synchronize();

    if(num_stride > 0) stride_list[stride_index].pass += stride_list[stride_index].stride;
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  //if(p->num_thread == 0) {
  //  swtch(&p->context, mycpu()->scheduler);
  //} else {
  swtch(&p->context, mycpu()->scheduler);
  //}
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      if(p->is_thread == 1) {
	p->parent->num_sleeping_thread--;
      }
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  //struct proc *pruning;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      //if(p->is_stride) {
      //  p->is_stride = 0;
      //  set_stride();
      //}
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  int i;
  uint sz, sp;
  uint ustack[2];
  pde_t *pgdir;
  struct proc *np;
  struct proc *curproc = myproc();
  
  if((np = allocproc()) == 0){
    return -1;
  }

  acquire(&ptable.lock);

  // for mess cleanup
  if(curproc->num_thread == 0) {
    curproc->prev_thread = np;
    curproc->old_sz = curproc->sz;
    curproc->tgid = next_tgid;
    next_tgid++;
  }

  sz = curproc->sz;
  pgdir = curproc->pgdir;

  if((sz = allocuvm(pgdir, sz, sz + 2 * PGSIZE)) == 0) return -1;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));

  sp = sz;
  sp -= 2*sizeof(uint);
  ustack[0] = 0xffffffff;
  ustack[1] = (uint)arg;
  if(copyout(pgdir, sp, ustack, 2*sizeof(uint))) return -1;
  
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  
  // ADDRESS SPACE SHARING //
  np->pgdir = curproc->pgdir;
  ///////////////////////////

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  np->tf->eip = (uint)start_routine;
  np->tf->esp = sp;
  np->tf->eax = 0;

  // if there isn't this procedure, thread cannot printf
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  np->state = RUNNABLE;
  
  np->is_thread = 1;
  np->is_stride = curproc->is_stride;
  np->mlfqlev = -1;
  
  np->tgid = curproc->tgid; // put into same thread group
  *thread = np->pid;
  curproc->num_thread++;

  //cprintf("created %d\n", curproc->num_thread);
  release(&ptable.lock);

  return 0;
}

void
thread_exit(void *retval)
{
  acquire(&ptable.lock);
  struct proc *curproc = myproc();
  if(curproc->is_thread != 1) panic("non-thread thread_exiting");
  
  curproc->retval = retval;
  curproc->state = ZOMBIE;
  curproc->parent->state = RUNNABLE;
  curproc->parent->num_thread--;
  
  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  swtch(&curproc->context, mycpu()->scheduler);
  panic("THIS SHOULDN'T");
}

int
thread_join(thread_t thread, void **retval)
{
  int sz;
  struct proc *p;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    //cprintf("<sleeping for %d>\n", thread);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->pid == thread)
	break;
    if(p->state == ZOMBIE) {
      //cprintf("thread termination confirmed\n");
      *retval = p->retval;
      kfree(p->kstack);
      p->kstack = 0;
      p->pid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = UNUSED;
      if(curproc->num_thread == 0) {
	//cprintf("no more threads left\n");
	if((sz = deallocuvm(curproc->pgdir, curproc->sz, curproc->old_sz)) == 0)
	  return -1;
	curproc->sz = sz;
	curproc->tgid = 0;
	multithreading = 0;
      }
      release(&ptable.lock);
      return 0;
    }
    sleep(myproc(), &ptable.lock);
  }
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
getppid(void)
{
    return myproc()->parent->pid;
}
