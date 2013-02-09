#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
/* Retrieve the n-th argument */
#define GET_ARGUMENT(sp, n) (*(sp + n))

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
  uint32_t arg1, arg2, arg3;
  
  /* Get syscall number */
  int syscall_no = *esp;
  switch(syscall_no)
  {
    case SYS_EXEC:
      arg1=GET_ARGUMENT(esp, 1);
      printf("argument is %s \n", (char*)arg1);
      f->eax = (uint32_t)_exec ((char*)arg1);
      break;

    case SYS_EXIT:
      arg1=GET_ARGUMENT(esp, 1);
      printf("argument is %d \n", arg1);
      _exit(arg1); break;

    case SYS_WAIT:
      arg1=GET_ARGUMENT(esp, 1);
      printf("argument is %d \n", arg1);
      f->eax = (uint32_t)_wait ((int)arg1);

    default:
      break;
  }
}

pid_t _exec (const char *cmd_line)
{
  /* Check address */
  if (!checkvaddr (cmd_line, 0) || !checkvaddr(cmd_line, strlen(cmd_line)))
    _exit (-1);
  pid_t pid = (pid_t) process_execute(cmd_line);
  if (pid == (pid_t) TID_ERROR)
    return -1;
  struct thread *t = thread_current();
  sema_down(&t->extra->sema_loaded);

  //arriving here means the child has finished loading
  if(t->extra->load_success == false)
    return -1;
  else
    return pid;
}

void _exit(int status)
{
  struct thread * cur_thread = thread_current();
  printf("%s: exit(%d)\n", cur_thread->name, status);
  cur_thread->extra->exit_status = status;
  thread_exit ();
}


int _wait(pid_t pid)
{
  process_wait(pid);
}




int
write(int fd, const void *buffer, unsigned size)
{

}


/* Check validity of buffer starting at vaddr, with length of size*/
static bool
  checkvaddr(const void * vaddr, unsigned size)
{
  /* If the address exceeds PHYS_BASE, exit -1 */
  if (!is_user_vaddr (vaddr + size)) 
    return false;

  return true;
}