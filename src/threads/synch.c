/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Random value for struct lock's `magic' member.
   Used to detect if the struct is a lock. */
#define LOCK_MAGIC 0xabcd1234

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}


//compare the wake_up_time of two threads from their list_elem
static bool
priority_greater_or_equal (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED)
{
	const struct thread *a = list_entry (a_, struct thread, elem);
	const struct thread *b = list_entry (b_, struct thread, elem);
	return (a->eff_priority) >= (b->eff_priority);
}

//find the max priority of a thread's lock list's owners and it's primitive priority 
int 
find_max_priority(struct thread *t )
{
	int max_priority = 0;
	struct list_elem *e;
	for( e = list_begin(&t->lock_list); e != list_end (&t->lock_list); e = list_next (e) )
	{
		struct lock *l = list_entry (e, struct lock, thread_elem);
		struct list_elem *frnt = list_front(&l->semaphore.waiters);
		struct thread *front_thread = list_entry (frnt, struct thread, elem);
		if (max_priority < front_thread->eff_priority)
			max_priority = front_thread->eff_priority;
	} 
	return ( max_priority > t->priority ) ? max_priority : t->priority;
}


//for nested (chaining) priority donation
void 
update_eff_priority (struct thread *update_who, int eff_priority)
{
	update_who->eff_priority = eff_priority;
	struct lock *l = update_who->lock_to_acquire;
	if (l !=NULL )
	{
		struct thread *owner = l->holder;
		struct list_elem *e;
		for( e=list_begin(&owner->lock_list); e != list_end (&owner->lock_list); e = list_next (e) )
		{
			struct lock *ll = list_entry (e, struct lock, thread_elem);
			list_sort(&ll->semaphore.waiters, priority_greater_or_equal, NULL);
		}
		if ( eff_priority > owner->eff_priority)
			update_eff_priority(owner, eff_priority );
		else
		{
			int p=find_max_priority(owner);
			if (p != owner->eff_priority)
				update_eff_priority(owner, p);
		}
	}
}



/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  ASSERT (sema != NULL);
  ASSERT (!intr_context ());
  old_level = intr_disable ();

  struct lock *l = get_sema_lock(sema);
  bool in_lock = (l->magic == LOCK_MAGIC);

  struct thread *holder=l->holder;
  printf("lock2 = %p\n", l);
  printf("lock2->holder = %p\n", holder);
  printf("lock2->semaphore = %p\n\n", &l->semaphore);
  printf("magic: %x\n\n", holder->magic);

  while (sema->value == 0) 
  {
    if (in_lock)
    {
	    //use sizeof to get the current owner of the lock:
	    struct thread *holder=l->holder;
	  
	    //check if this lock is in the owner's lock_list:
	    printf("checking if holder->lock_list contains this lock...\n\n ");
	    printf("lock3 = %p\n", l);
	    printf("lock3->holder = %p\n", l->holder);
	    printf("lock3->semaphore = %p\n", &l->semaphore);

	    ASSERT(holder != NULL);
	    printf("holder magic: %x\n\n", holder->magic);
	    printf("empty? %d\n", list_empty(&holder->lock_list));
	    if ( ! list_elem_exist( &holder->lock_list, &l->thread_elem))
		    //if not, insert it in the lock_list
	    {
		    printf("don't contain, insert...");
		    list_push_front( &holder->lock_list,  &l->thread_elem);
			  printf("don't contain, insert finished");
	    }
		
	    //put the current thread to this lock's waiting queue (>=sorted)
	    list_insert_ordered(&(sema->waiters), &thread_current()->elem, priority_greater_or_equal, NULL);

	    if (thread_current()->eff_priority > holder->eff_priority)
      {
	        update_eff_priority (holder, thread_current()->eff_priority);
      }
    }
    else
    {
      //put the current thread to this lock's waiting queue (>=sorted)
      list_insert_ordered(&(sema->waiters), &thread_current()->elem, priority_greater_or_equal, NULL);
    }
    thread_block ();
  }
  sema->value--;

  if (in_lock)
  {
    thread_current()->lock_to_acquire = NULL;
    l->holder = thread_current ();
  }
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
  {
	  //use sizeof to get which lock am I trying to acquire:
	  struct lock *l=get_sema_lock(sema);
	  //use sizeof to get the current owner of the lock:
	  struct thread *holder=get_sema_holder(sema);
	  ASSERT (holder == thread_current());
	  struct thread *to_be_unblocked=list_entry(list_front(&sema->waiters), struct thread, elem);
	  //see if the holder's contributor is the thread to_be_unblocked
	  if ( holder->eff_priority <= to_be_unblocked->eff_priority)
	  {		 
		 int priority=find_max_priority(holder);
         update_eff_priority (holder, priority);
	  }
	  //the current thread no longer has L in the lock_list
	  list_remove(&l->thread_elem);// to be verified

	  thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
  }
  sema->value++;
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->magic = LOCK_MAGIC;
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
  //for priority scheduling, lock->thread_elem needn't init	  
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  thread_current()->lock_to_acquire=lock;
  printf("lock = %p\n", lock);
  printf("lock->holder = %p\n", lock->holder);
  printf("lock->semaphore = %p\n", &lock->semaphore);
  printf("sizeof(struct thread *) = %d\n", sizeof(struct thread *));
  
  sema_down (&lock->semaphore);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
