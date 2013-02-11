#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "threads/fixed-point.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list[64];

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* The average number of threads ready to run over the past minute */
static int load_avg;


/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  int i;
  lock_init (&tid_lock);
  for (i=0; i<64; i++)
    list_init (&ready_list[i]);

  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  load_avg = 0;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  init_exit_status (initial_thread, initial_thread->tid);

  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
  if (!init_exit_status (t, tid))
    return TID_ERROR;

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);
  
  if(thread_mlfqs)
  {
    if (thread_current()->priority < t->priority)
      thread_yield ();
  }
  else
  {
    if (thread_current()->eff_priority < t->eff_priority)
      thread_yield ();
  } 

  return tid;
}

/* Initialize the exit_status */
bool init_exit_status(struct thread *t, tid_t tid)
{
  struct exit_status* es;
  es = (struct exit_status*) malloc(sizeof(struct exit_status));
  if (es == NULL)
    return false;
  t->exit_status = es;
  es->pid = tid;
  es->exit_value = 0;
  sema_init( &es->sema_wait, 0);
  es->ref_counter = 2;
  lock_init ( &es->counter_lock );
  es->list_lock = &thread_current() ->list_lock;
  return true;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if(thread_mlfqs ){
    list_push_back (&ready_list[63- (t->priority)], &t->elem);
  }
  else{
    list_push_back (&ready_list[63- (t->eff_priority)], &t->elem);
  }
  t->status = THREAD_READY;
  intr_set_level (old_level);

}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
  {
    if (thread_mlfqs)
    {
      list_push_back (&ready_list[ 63-(cur->priority) ], &cur->elem);
    } 
    else
    {
      list_push_back (&ready_list[ 63-(cur->eff_priority) ], &cur->elem);
    }
  }
   
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  enum intr_level old_level;
  old_level = intr_disable ();
  
  bool yield_on_return = false;
  struct thread *t = thread_current ();
  int old_priority = t->priority;
  if (new_priority == t->priority)
    return;
  t->priority = new_priority;
  if (!thread_mlfqs)
  {

      if (new_priority > thread_current()->eff_priority)
      {
        thread_set_eff_priority (t, new_priority);
      }
      else
      {
        int new_eff_priority = thread_find_max_priority (t);
        thread_set_eff_priority (t, new_eff_priority);
        yield_on_return = true;
      }
  }
  else
  {
      if (new_priority < old_priority)
		yield_on_return = true;

  }
  intr_set_level (old_level);

  if (yield_on_return)
    thread_yield();
}

/* Update the effective priority of a thread. If the thread to update is
   waiting for a lock held by another thread, priority donation will be
   triggered. */
void
thread_set_eff_priority (struct thread *t, int eff_priority)
{
  if (t->eff_priority == eff_priority) {
    return;
  }

  t->eff_priority = eff_priority;
  struct lock *l = t->lock_to_acquire;

  if (l != NULL)
  {
    struct thread *holder = l->holder;
    list_sort (&l->semaphore.waiters, priority_greater_or_equal, NULL);
    if (eff_priority > holder->eff_priority)
    {
      thread_set_eff_priority (holder, eff_priority);
    }
    else if (eff_priority < holder->eff_priority)
    {
      int p = thread_find_max_priority (holder);
      if (p != holder->eff_priority)
      {
        thread_set_eff_priority (holder, p);
      }
    }
  }
}

/* Compare the effective priority of two threads given their list_elem.
   Returns true if A is larger or equal to B, or false if A is less
   than B. */
bool
priority_greater_or_equal (const struct list_elem *a,
                           const struct list_elem *b,
                           void *aux UNUSED)
{
  const struct thread *ta = list_entry (a, struct thread, elem);
  const struct thread *tb = list_entry (b, struct thread, elem);
  return thread_mlfqs? ( ta->priority >= tb->priority ): ( ta->eff_priority >= tb->eff_priority );
}

/* Return the max priority of all other threads waiting for locks held by
   the given thread and its primitive priority */
int
thread_find_max_priority (struct thread *t)
{
  int max_priority = 0;
  struct list_elem *e;
  for( e = list_begin(&t->locks_waited_by_others);
       e != list_end (&t->locks_waited_by_others);
       e = list_next (e) )
  {
    struct lock *l = list_entry (e, struct lock, thread_elem);
    struct list_elem *frnt = list_front(&l->semaphore.waiters);
    struct thread *front_thread = list_entry (frnt, struct thread, elem);
    if (max_priority < front_thread->eff_priority)
      max_priority = front_thread->eff_priority;
  }
  return ( max_priority > t->priority ) ? max_priority : t->priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  if(thread_mlfqs)
    return thread_current ()->priority;
  else
    return thread_current ()->eff_priority;
}

/* Calculate advanced priority for a thread */
void
calculate_priority_advanced(struct thread * th)
{
  ASSERT(thread_mlfqs);
  if(th != idle_thread)
  {
    th->priority = PRI_MAX - 
    CONVERT_TO_INT_DOWN(DIV_INT(th->recent_cpu, 4)) - th->nice * 2;
    if(th->priority > PRI_MAX)
      th->priority = PRI_MAX;
    if(th->priority < PRI_MIN)
      th->priority = PRI_MIN;
    if ( th!=thread_current() && th->status == THREAD_READY)
    {
      list_remove(&th->elem);      
      list_push_back (&ready_list[63- (th->priority)], &th->elem);
    }
  }
}

/* Calculate recent_cpu for a thread */
void
calculate_recent_cpu(struct thread * th)
{ 
  ASSERT(thread_mlfqs);
  ASSERT(is_thread(th));
  if(th != idle_thread)
  {
    int load_2 = MUL_INT(load_avg, 2);
    int coefficient = DIV_FP(load_2, ADD_INT(load_2, 1));
    int part_a = MUL_FP(coefficient, th->recent_cpu);
    th->recent_cpu = ADD_INT(part_a, th->nice);
  }
}

/* Calculate recent_cpu for all threads */
void 
calculate_recent_cpu_all(void)
{
  thread_foreach(calculate_recent_cpu, NULL);
}

/* Advanced scheduling: calculate priority for all threads */
void 
calculate_priority_advanced_all(void)
{
  thread_foreach(calculate_priority_advanced, NULL);
}


int
get_num_ready_threads(void)
{
    int i;
    int length_all;
    length_all = 0;
    for (i = 0; i < 64; i++)
    {
      length_all = length_all + list_size( &ready_list[i]);
    }
    return length_all;
}

/* Calculate load_avg */
void
calculate_load_avg(void)
{
  ASSERT(thread_mlfqs);
  struct thread * cur_thread;
  cur_thread = thread_current();
  int ready_threads;
  ready_threads = get_num_ready_threads();
  if(cur_thread != idle_thread)
  {
    ready_threads = ready_threads + 1;
  }
  int part_a = MUL_FP(DIV_INT(CONVERT_TO_FP(59), 60), load_avg);
  int part_b = MUL_INT(DIV_INT(CONVERT_TO_FP(1), 60), ready_threads);
  load_avg = ADD_FP(part_a, part_b);
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  ASSERT(thread_mlfqs);
  struct thread * cur_thread = thread_current();
  int old_priority = cur_thread->priority;

  cur_thread->nice = nice;
  calculate_priority_advanced(cur_thread);

  if (cur_thread->priority < old_priority)
    thread_yield();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return CONVERT_TO_INT_NEAREST( MUL_INT(load_avg, 100) );
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return CONVERT_TO_INT_NEAREST( MUL_INT(thread_current()->recent_cpu, 100) );
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  if(thread_mlfqs)
  {
    if(t == initial_thread)
    {
      t->nice = 0;
      t->recent_cpu = 0;
    }
    else
    {
      t->nice = thread_get_nice();
      t->recent_cpu = thread_current()->recent_cpu;
    }
  }

  t->eff_priority = priority;
  t->lock_to_acquire = NULL;
  list_init (&(t->locks_waited_by_others));
  list_init (&t->child_exit_status);
  lock_init (&t->list_lock);

  /* Lazy allocation */
  t->file_handlers = NULL;
  t->file_handlers_size = 0;
  t->file_handlers_num = 0;

  t->is_kernel = true;
  t->in_syscall = false;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  int i;
  for (i = 0; i < 64; i++)
  {
    if (!list_empty ( &ready_list[i] ))
      return list_entry (list_pop_front (&ready_list[i]), struct thread, elem);
  }
  return idle_thread; 
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

bool
valid_file_handler (struct thread* thread, int fd)
{
  if (fd == 0 || fd == 1)
    return true;
  if (fd < 0)
    return false;
  if (thread == NULL ||
      fd >= thread->file_handlers_size ||
      thread->file_handlers[fd] == NULL)
    return false;

  return true;
}

int
thread_add_file_handler (struct thread* thread, struct file* file)
{
  if (file == NULL || thread == NULL)
    _exit (-1);

  if (thread->file_handlers_num < thread->file_handlers_size)
  {
    int i;
    for (i = 2; i < thread->file_handlers_size; i++)
    {
      if (thread->file_handlers[i] == NULL)
      {
        thread->file_handlers[i] = file;
        thread->file_handlers_num ++;
        return i;
      }
    }
    /* Should never arrive here */
    NOT_REACHED();
    return -1;
  }
  else
  {
    ASSERT (thread->file_handlers_num == thread->file_handlers_size);
    struct file **new_file_handlers;
    if (thread->file_handlers == NULL) /* Allocate file_handlers for 1st time */
    {
      thread->file_handlers_size = FILE_HDL_SIZE;
      new_file_handlers = (struct file**)
          malloc (FILE_HDL_SIZE * sizeof(struct file*));
      memset (new_file_handlers, 0, FILE_HDL_SIZE * sizeof(struct file*));
      thread->file_handlers_num = 2;
    }
    else /* Double the space once full */
    {
      thread->file_handlers_size *= 2;
      new_file_handlers = (struct file**)
          malloc (thread->file_handlers_size * sizeof(struct file*));
      memcpy (new_file_handlers, thread->file_handlers,
              thread->file_handlers_num * sizeof(struct file*));
      memset (&new_file_handlers[thread->file_handlers_num], 0,
              thread->file_handlers_num);
    }
    thread->file_handlers = new_file_handlers;
    int index = thread->file_handlers_num;
    thread->file_handlers[index] = file;
    return index;
  }
}

void
thread_remove_file_handler (struct thread* thread, int fd)
{
  if (!valid_file_handler (thread, fd))
    _exit (-1);

  thread->file_handlers[fd] = NULL;
  thread->file_handlers_num --;
}

