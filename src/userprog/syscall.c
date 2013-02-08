#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* convert ESP to a int pointer */
  int * esp = (int *)f->esp;

  switch(*esp){
    case SYS_EXIT:
      exit(GET_ARGUMENT(esp, 1));
      break;
    default:
      break;
  }
}

static void exit(int status){
  struct thread * cur_thread = thread_current();
  printf("%s: exit(%d)\n", cur_thread->name, status);
  thread_exit ();
}


int
wait(pid_t pid){

}

int
write(int fd, const void *buffer, unsigned size){

}
