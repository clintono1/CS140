#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define NICE_MAX 20                     /* Highest nice */
#define NICE_MIn -20                    /* Lowest nice */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */

//struct load_success
//{
//	semaphore sema_load;
//	bool is_load_success; 
//};

struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    int64_t wake_up_time;               /* Time to wake up current thread */
    struct list_elem alarm_elem;        /* List element for alarm queue */
    int eff_priority;                   /* Effective priority */
    struct lock * lock_to_acquire;      /* Lock this thread is waiting for */
    struct list locks_waited_by_others; /* Locks held by this thread but
                                           also waited by other threads*/
    int recent_cpu;                     /* CPU time received recently */
    int nice;                           /* Nice value of each thread*/

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
	//maybe these two items could be wrapped with a structure?
	//the function of this extra_data * is 指向thread的遗产。即便thread死了，他的exit_data保存在遗产结构体中（extra_data）
	//这样wait他的进程就可以取出遗产中的exit_data。清理遗产比较tricky：current policy is that we free the extra_data when the 
	//parent died, therefore the parent should signal the child that their extra_data no longer exist, so dont's try to access this 
	//extra data (mainly to write it's exit_status and to sema up the sema_exited when it exits). This is achieved by writting the 
	//owner thread's extra_data pointer to NULL. But this owner_thread is not always not NULL, consider if the owner thread has
	//already died. 

	struct extra_data* extra_data;
	struct lock extra_data_lock;

#endif
    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

struct extra_data
{
	pid_t process_id;
	bool was_waited;//might be elimintated.
	int exit_status;
	//struct load_success * load_success;
	semaphore sema_exited;
	semaphore sema_loaded;
	bool load_success;
	

	struct thread * owner_thread;
	struct lock owner_thread_lock;
};
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);
void thread_set_eff_priority (struct thread *, int);
int thread_find_max_priority (struct thread *t);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

bool priority_greater_or_equal (const struct list_elem *,
                                const struct list_elem *,
                                void *);

int get_num_ready_threads(void);
void calculate_load_avg (void);
void calculate_recent_cpu(struct thread * th);
void calculate_recent_cpu_all(void);
void calculate_priority_advanced(struct thread * th);
void calculate_priority_advanced_all(void);

#endif /* threads/thread.h */
