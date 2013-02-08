#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static void exit(int status);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  switch(*((int *)f->esp)){
   case SYS_EXIT:
     exit(0);
     break;
  }
}

static void exit(int status){
  struct thread * cur_thread = thread_current();
  printf("%s: exit(%d)\n", cur_thread->name, status);
  thread_exit ();
}
