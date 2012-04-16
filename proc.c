#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
//#include "defhandlers.c"
#include "signal.h"




//prototypes:
int sigsend(int, int);

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

#include "defhandlers.c"



static struct proc *initproc;
static int lastShInvokedPid = -1;


int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}


/* auxilary function for getting the bitwise number */
int
getSigBitwise(int signum){
	int ans = 1;
	return ans << signum;
}

/* ausxilary function for xoring 2 operators */
int
getBitwiseXor(int a, int b){
	return a ^ b;
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
  int i;
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
  
  p->pendingSignals = 0;
  for (i=0; i< NUM_OF_SIGNALS; i++)
	p->signalHandlers[i] = (sighandler_t) SIG_IGN;
  p->signalHandlers[0] =  (sighandler_t) SIG_DFL;
  p->signalHandlers[1] =  (sighandler_t) SIG_DFL;
  p->signalHandlers[2] =  (sighandler_t) SIG_DFL;
  p->signalHandlers[3] =  (sighandler_t) SIG_DFL;
  p->signalHandlers[12] = (sighandler_t) SIG_DFL;
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
  if((p->pgdir = setupkvm(kalloc)) == 0)
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

  p->state = RUNNABLE;
  
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
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

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;
  
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
 
  pid = np->pid;
  
  /* task 2.5 */
  
  np->pendingSignals = proc->pendingSignals;
  for (i = 0; i< NUM_OF_SIGNALS; i++)
	np->signalHandlers[i] = proc->signalHandlers[i];
  
  /* end task 2.5 */
  
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  
  if (strncmp(proc->name, "sh", 3) == 0)
	lastShInvokedPid = np->pid;
  
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  iput(proc->cwd);
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  lastShInvokedPid = -1;
  proc->state = ZOMBIE;
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

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

void
register_handler(sighandler_t sighandler)
{
  char* addr = uva2ka(proc->pgdir, (char*)proc->tf->esp);
  if ((proc->tf->esp & 0xFFF) == 0)
    panic("esp_offset == 0");

    /* open a new frame */
  *(int*)(addr + ((proc->tf->esp - 4) & 0xFFF))
          = proc->tf->eip;
  proc->tf->esp -= 4;

    /* update eip */
  proc->tf->eip = (uint)sighandler;
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
  struct proc *p;
  int pPendingSignals;
  sighandler_t handler;
  sighandler_t defaultHandlers[32]; //32 default functions for handling signals the default way
  int i;
  int bitwiseSig;
  int registeredFlag = 0; //anotates that the signal that was handeled was a user signal
  //registering default handlers:
  for(i = 0; i< NUM_OF_SIGNALS; i++)
	  defaultHandlers[i] = doNothingHandler;
  	  
  defaultHandlers[0] = sigIntHandler;
  defaultHandlers[1] = sigUsr1Handler;
  defaultHandlers[2] = sigUsr2Handler;
  defaultHandlers[3] = sigChldHandler;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
	  
	  
	  
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      
      pPendingSignals = p->pendingSignals;
	  for (i = 0; i < NUM_OF_SIGNALS; i++){
		registeredFlag = 0;
		bitwiseSig = getSigBitwise(i);
		if ((bitwiseSig & pPendingSignals) > 0){
			//cprintf("KING: %d", (int)p->signalHandlers[i]);
			if ((int)p->signalHandlers[i] == SIG_DFL){
				//need to take DFL action, it is clear to the process, it does not know that the signal was handeled
				handler = defaultHandlers[i];
				(*handler)();
				p->pendingSignals = getBitwiseXor(p->pendingSignals, bitwiseSig);
				break;
			}
			
			else{
				if ((int)p->signalHandlers[i] == SIG_IGN){
			         p->pendingSignals = getBitwiseXor(p->pendingSignals, bitwiseSig);
					 break;
				}
				else{
					register_handler(p->signalHandlers[i]);
					registeredFlag = 1;
					break;
					//todo XOR to make signal go away - AFTER CPU GETS TIME BEFORE PROC = 0!!
				}
			}
		}
	  }
      
      
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();
	  if (registeredFlag == 1){
		proc->pendingSignals = getBitwiseXor(proc->pendingSignals, bitwiseSig);
		proc->signalHandlers[i] = (sighandler_t) SIG_DFL;
	  }
	  
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
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
    initlog();
  }
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
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
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

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
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
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

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
	  }
	  /* task 3 */
	  release(&ptable.lock);
	  sigsend(proc->parent->pid, SIGCHLD);
	  acquire(&ptable.lock);
	  /* task 3 */
	  
	  lastShInvokedPid = -1;
	  
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}






//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
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
signal(int signum, sighandler_t handler){
	if (signum >= 0 && signum <=31){
		proc->signalHandlers[signum] = handler;
		return 0;
	}
	else return -1;
}

int
sigsend(int pid, int signum){
	//find the process with the pid in the ptable
	struct proc *p;
	//int i;
	
	int flag = 0; //flaging if we broke the loop, if we didn't, pid does not exist
	int sigBitwise = getSigBitwise(signum);
	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if (p->pid == pid){
			flag = 1;
			break;
		}
	}
	
	if (flag == 0){
		release(&ptable.lock);
		return -1;
	}
	p->pendingSignals = p->pendingSignals | sigBitwise;
	
	release(&ptable.lock);
	return 0;
}


void ctrlc(void){
	if(lastShInvokedPid != -1)
		sigsend(lastShInvokedPid, SIGINT); 

}
