// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct node {
  int val;
  struct node *link;
};

struct queue
{
  struct node *front, *rear;
};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)

  // MLFQ
  int mlfqlev;			// MLFQ level of the current process
  int allotment;		// allotment
  int stampin;			// timestamp, used to update runtime
  int stampout;			// timestamp, used to update runtime
  // need to implement
  int hitzone;			// Dangerous
  int strike;			// STRIKE!!

  // Stride
  int is_stride;		// Identifier
  int share;			// Share

  // Thread //////////////////////////////////////////////////////////////
  uint t_history;		// Till what number is the thread created?
  uint t_sz_accumulative;	// keep track of the actual total size;
  uint t_sz[NTHREAD];	// for moving thread ustack
  enum procstate t_state[NTHREAD];  // keep thread state here?
  char *t_kstack[NTHREAD]; // thread kstack
  // char *t_ustack[NTHREAD]; // no need
  struct trapframe *t_tf[NTHREAD];
  struct context *t_context[NTHREAD];
  uint old_sz;

  void *stack;

  // thread run
  int proc_true; // if it is proc that is running
  int num_thread; // initialized with zero
  int active_thread;
  uint t_chan; // thread sleeps on this, -1 if sleeping on nothing

  // thread's dying message
  void *dyingmessage[NTHREAD];
  // Thread //////////////////////////////////////////////////////////////

};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
