#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
bool valid_vaddr_range(const void * vaddr, unsigned size);

void  _halt (void);
void  _exit (int status);
pid_t _exec (const char *cmd_line);
int   _wait (pid_t pid);
bool  _create (const char *file, unsigned initial_size);
bool  _remove (const char *file);
int   _open (const char *file);
int   _filesize (int fd);
int   _read (int fd, void *buffer, unsigned size);
int   _write (int fd, const void *buffer, unsigned size);
void  _seek (int fd, unsigned position);
unsigned _tell (int fd);
void  _close (int fd);

struct lock global_lock_filesys;  /* global lock for file system*/

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&global_lock_filesys);
}
/* Check then Retrieve the n-th argument */
static uint32_t
 get_argument (int *esp, int offset)
{
  /* Check address */
  if (!is_user_vaddr (esp + offset)) 
    _exit(-1);

  return *(esp + offset);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{

  /* Convert ESP to a int pointer */
  int * esp = (int *)f->esp;
  uint32_t arg1, arg2, arg3;

  if ( !valid_vaddr_range(esp, 0) )
    _exit(-1);

  struct thread *t = thread_current();
  t->in_syscall = true;

  /* Get syscall number */
  int syscall_no = *esp;

  switch(syscall_no)
  {
    case SYS_HALT:
      _halt ();
      break;

    case SYS_EXIT:
      arg1 = get_argument(esp, 1);
      _exit (arg1);
      break;

    case SYS_EXEC:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _exec ((char*) arg1);
      break;

    case SYS_WAIT:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _wait ((int) arg1);
      break;

    case SYS_CREATE:
      arg1 = get_argument(esp, 1);
      arg2 = get_argument(esp, 2);
      f->eax = (uint32_t) _create ((const char*)arg1, (unsigned)arg2);
      break;

    case SYS_REMOVE:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _remove ((const char*)arg1);
      break;

    case SYS_OPEN:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _open ((const char*)arg1);
      break;

    case SYS_FILESIZE:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _filesize ((int)arg1);
      break;

    case SYS_READ:
      arg1 = get_argument(esp, 1);
      arg2 = get_argument(esp, 2);
      arg3 = get_argument(esp, 3);
      f->eax = (uint32_t) _read ((int)arg1, (void*)arg2, (unsigned)arg3);
      break;

    case SYS_WRITE:
      arg1 = get_argument(esp, 1);
      arg2 = get_argument(esp, 2);
      arg3 = get_argument(esp, 3);
      f->eax = (uint32_t) _write ((int)arg1, (const void*)arg2, (unsigned)arg3);
      break;

    case SYS_SEEK:
      arg1 = get_argument(esp, 1);
      arg2 = get_argument(esp, 2);
      _seek ((int)arg1, (unsigned)arg2);
      break;

    case SYS_TELL:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _tell ((int)arg1);
      break;

    case SYS_CLOSE:
      arg1 = get_argument(esp, 1);
      _close ((int)arg1);
      break;
                                         
    default:
      break;
  }
  t->in_syscall = false;
}

/* Return true if virtual address range [vaddr, vadd+size] is valid */
inline bool
valid_vaddr_range(const void * vaddr, unsigned size)
{
  /* false type1: null pointer: */
  if (vaddr == NULL)
    return false;
  /* false type2: points to KERNEL virtual address space */
  if (!is_user_vaddr (vaddr) || !is_user_vaddr (vaddr + size))
    return false;
  /* false type3: points to unmapped virtual memory */
  if (!pagedir_get_page (thread_current()->pagedir, vaddr) || \
      !pagedir_get_page (thread_current()->pagedir, (vaddr + size) ))
    return false;
  return true;
}

/* Part1:syscalls for process control*/
void
_halt (void)
{
  shutdown_power_off();
}

pid_t
_exec (const char *cmd_line)
{
  /* Check address */
  if (!valid_vaddr_range (cmd_line, 0))
    _exit (-1);

  if (!valid_vaddr_range(cmd_line, strlen(cmd_line)))
    _exit (-1);
  char file_path[MAX_FILE_LENGTH];
  get_first_string(cmd_line, file_path);
  /* Lock on process_execute since it needs to open the executable file */
  lock_acquire (&global_lock_filesys);
  struct file *file = filesys_open (file_path);
  if (file == NULL ) 
  { 
     lock_release (&global_lock_filesys);
     return -1; 
  }
  pid_t pid = (pid_t) process_execute(cmd_line);
  lock_release (&global_lock_filesys);

  if (pid == (pid_t) TID_ERROR)
    return -1;
  return pid;
}

void
_exit(int status)
{
  struct thread * cur_thread = thread_current();
  cur_thread->exit_status->exit_value = status;
  thread_exit ();
}

int
_wait(pid_t pid)
{
  return process_wait(pid);
}


/* Part2: syscalls for file system */
bool
_create (const char *file, unsigned initial_size)
{
  
  if (!valid_vaddr_range (file, 0))
    _exit (-1);

  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  lock_acquire (&global_lock_filesys);
  bool success = filesys_create (file, initial_size);
  lock_release (&global_lock_filesys);
  return success;
}

bool
_remove (const char *file)
{
  if (!valid_vaddr_range (file, 0))
    _exit (-1);

  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  lock_acquire (&global_lock_filesys);
  bool success = filesys_remove (file);
  lock_release (&global_lock_filesys);
  return success;
}

int
_open (const char *file)
{
  if (!valid_vaddr_range (file, 0))
    _exit (-1);

  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  lock_acquire (&global_lock_filesys);
  struct file *f = filesys_open (file);
  lock_release (&global_lock_filesys);

  if (f == NULL)
    return -1;

  return thread_add_file_handler (thread_current(), f);
}

int
_filesize (int fd)
{
  struct thread* t = thread_current();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    _exit (-1);

  lock_acquire (&global_lock_filesys);
  int size = (int) file_length (t->file_handlers[fd]);
  lock_release (&global_lock_filesys);

  return size;
}

int
_read (int fd, void *buffer, unsigned size)
{
  if (!valid_vaddr_range (buffer, size))
     _exit (-1);

  if (size < 0)
    return -1;
  if (fd == STDOUT_FILENO)
    return -1;

  int result = 0;
  struct thread *t=thread_current();
  if (fd == STDIN_FILENO)
  {
      unsigned i = 0;
      for (i = 0; i < size; i++)
      {
        *(uint8_t *)buffer = input_getc();
        result++;
        buffer++;
      }
      return result;
  }

  else if(valid_file_handler(t, fd))
  {
      struct file *file = t->file_handlers[fd];
      lock_acquire(&global_lock_filesys );
      result = file_read(file, buffer, size);
      lock_release(&global_lock_filesys);
      return result;
  }
  return -1;
}

int
_write (int fd, const void *buffer, unsigned size)
{
  if (!valid_vaddr_range (buffer, size))
    _exit (-1);
  if (size <= 0)
    return 0;
  if (fd == STDIN_FILENO)
    return -1;

  int result = 0;
  struct thread *t = thread_current ();
  if (fd == STDOUT_FILENO)
  {
    putbuf (buffer, size);
    result = size;
  }
  else if (valid_file_handler (t, fd))
  {
    struct file *file = t->file_handlers[fd];
    lock_acquire (&global_lock_filesys);
    result = file_write (file, buffer, size);
    lock_release (&global_lock_filesys);
  }
  return result;
}

void
_seek (int fd, unsigned position)
{
  struct thread *t = thread_current ();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

   lock_acquire (&global_lock_filesys);
   file_seek (file, position);
   lock_release (&global_lock_filesys);

}

unsigned
_tell (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

  lock_acquire (&global_lock_filesys);
  off_t n= file_tell (file);
  lock_release (&global_lock_filesys);
  return n;
}

void
_close (int fd)
{
  struct thread* t = thread_current();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    return;

  lock_acquire (&global_lock_filesys);
  file_close (t->file_handlers[fd]);
  lock_release (&global_lock_filesys);

  thread_remove_file_handler (t, fd);
}

