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
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/input.h"
#include "threads/pte.h"

static void syscall_handler (struct intr_frame *);
static bool
  check_user_memory (const void *vaddr, size_t size, bool to_be_written);
static inline bool valid_vaddr_range(const void * vaddr, unsigned size);

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
bool _chdir (const char *dir);
bool _mkdir (const char *dir);
bool _readdir (int fd, char *name);
bool _isdir (int fd);
int  _inumber (int fd);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
    
    case SYS_CHDIR:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _chdir ((const char*)arg1);
      break;
    
    case SYS_MKDIR:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _mkdir ((const char*)arg1);
      break;
    
    case SYS_READDIR:
      arg1 = get_argument(esp, 1);
      arg2 = get_argument(esp, 2);
      f->eax = (uint32_t) _readdir ((int)arg1, (char*)arg2);
      break;

    case SYS_ISDIR:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _isdir((int)arg1);
      break;

    case SYS_INUMBER:
      arg1 = get_argument(esp, 1);
      f->eax = (uint32_t) _inumber((int)arg1);
      break;

    default:
      break;
  }
  t->in_syscall = false;
}

/* Return true if virtual address range [vaddr, vadd+size] is valid */
static inline bool
valid_vaddr_range (const void * vaddr, unsigned size)
{
  /* false type1: null pointer: */
  if (vaddr == NULL)
    return false;
  /* false type2: points to KERNEL virtual address space */
  if (!is_user_vaddr (vaddr) || !is_user_vaddr (vaddr + size))
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
  struct file *file = filesys_open (file_path);
  if (file == NULL ) 
  { 
     return -1; 
  }
  pid_t pid = (pid_t) process_execute(cmd_line);

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

  bool success = filesys_create (file, initial_size);
  return success;
}

int
_open (const char *file)
{
  if (!valid_vaddr_range (file, 0))
    _exit (-1);
  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  struct file *f = filesys_open (file);

  if (f == NULL)
    return -1;

  return thread_add_file_handler (thread_current(), f);
}

bool
_remove (const char *name)
{
  if (!valid_vaddr_range (name, 0))
    _exit (-1);

  if (!valid_vaddr_range (name, strlen (name)))
    _exit (-1);

  bool success = filesys_remove (name);
  return success;
}

int
_filesize (int fd)
{
  struct thread* t = thread_current();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    _exit (-1);

  int size = (int) file_length (t->file_handlers[fd]);

  return size;
}

int
_read (int fd, void *buffer, unsigned size)
{
  if (!valid_vaddr_range (buffer, size))
     _exit (-1);

  if (!check_user_memory (buffer, size, true))
    _exit (-1);

  if (fd == STDOUT_FILENO)
    return -1;

  int result = 0;
  struct thread *t = thread_current();
  if (fd == STDIN_FILENO)
  {
      unsigned i = 0;
      for (i = 0; i < size; i++)
      {
        *(uint8_t *) buffer = input_getc();
        result ++;
        buffer ++;
      }
      return result;
  }
  else if(valid_file_handler (t, fd))
  {
      struct file *file = t->file_handlers[fd];
      result = file_read(file, buffer, size);
      return result;
  }
  return -1;
}

int
_write (int fd, const void *buffer, unsigned size)
{
  if (!valid_vaddr_range (buffer, size))
    _exit (-1);

  if (!check_user_memory (buffer, size, false))
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
    bool is_dir = (inode_is_dir(file_get_inode(file)));
    
    if (is_dir)
    {
      return -1;
    }
    
    result = file_write (file, buffer, size);

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

   file_seek (file, position);

}

unsigned
_tell (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

  off_t n= file_tell (file);
  return n;
}

void
_close (int fd)
{
  struct thread* t = thread_current();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    return;

  file_close (t->file_handlers[fd]);

  thread_remove_file_handler (t, fd);
}

/* Part3: syscalls for sub-directories */
//TODO: no sync yet!
bool
_chdir (const char *name)
{
  if (!valid_vaddr_range (name, 0))
    _exit (-1);
  if (!valid_vaddr_range (name, strlen (name)))
    _exit (-1);
  if (!strcmp(name, "/"))
  {
    thread_current()->cwd_sector = ROOT_DIR_SECTOR;
    return true;
  }

  struct dir *dir;
  char *dir_name;
  if (!filesys_parse (name, &dir, &dir_name))
    return false;

  struct inode *inode = NULL;
  if (!dir_lookup (dir, dir_name, &inode))
  {
    dir_close (dir);
    free (dir_name);
    return false;
  }
  dir_close (dir);
  free (dir_name);
  thread_current ()->cwd_sector = inode_get_inumber (inode);
  inode_close (inode);
  return true;
}

bool
_mkdir (const char *name)
{
  bool success = false;
  if (!valid_vaddr_range (name, 0))
    _exit (-1);
  if (!valid_vaddr_range (name, strlen (name)))
    _exit (-1);
  
  if (!strcmp(name, "") || !strcmp(name, "/"))
    return false;
  block_sector_t inode_sector = 0;
  struct dir *dir, *new_dir;
  char *dir_name;
  
  if (!filesys_parse (name, &dir, &dir_name))
    return false;

  if (! (dir != NULL && free_map_allocate (1, &inode_sector)))
  {
    dir_close (dir);
    free (dir_name);
    return false;
  }
  /* Write inode to this sector. */  
  if (inode_create (inode_sector, 0, true))
  {
    /* Add this inode to parent dir */
    success = dir_add (dir, dir_name, inode_sector, true);
    /* Add . and .. to the new directory */
    if (success)
    {
      new_dir  = dir_open (inode_open (inode_sector));
      success &= dir_add (new_dir, ".", inode_sector, true);
      if (success)
        success &= dir_add (new_dir, "..",
                            inode_get_inumber(dir_get_inode(dir)),
                            true);
      dir_close (new_dir);
      if (!success)
        dir_remove (dir, dir_name);
    }
  }
  dir_close (dir);
  free (dir_name);
  return success;
}

bool
_readdir (int fd, char name[READDIR_MAX_LEN + 1])
{
  if (!valid_vaddr_range (name, 0))
    _exit (-1);
  if (!valid_vaddr_range (name, strlen (name)))
    _exit (-1);

  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];
  off_t pos = file_tell(file);
  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

  struct inode *inode = file_get_inode(file);
  /* Make sure we are reading a directory, not a file*/
  if (!inode_is_dir(inode))
    return false;
  struct dir *dir = dir_open(inode);
  dir_set_pos(dir, pos);
  bool success = dir_readdir(dir, name);
  file_seek(file, dir_get_pos(dir));
  free(dir);
  return success;
}

bool
_isdir (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];
  
  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);
  bool is_dir = (inode_is_dir(file_get_inode(file)));
  return is_dir;
}

int
_inumber (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

  int inumber = (inode_get_inumber(file_get_inode(file)));
  return inumber;
}

/* Check whether specified user memory range [ADDR, ADDR + SIZE) is valid. */
static bool
check_user_memory (const void *vaddr, size_t size, bool to_be_written)
{
  if (vaddr + size > PHYS_BASE)
    return false;

  void *upage = pg_round_down (vaddr);

  while (upage < vaddr + size)
  {
    uint32_t *pte = lookup_page (thread_current()->pagedir, upage, false);
    if (pte == NULL || *pte == 0)
      return false;
    if (!(*pte & PTE_P) || !(*pte & PTE_U))
      return false;
    if (to_be_written && !(*pte & PTE_W))
      return false;
    upage += PGSIZE;
  }
  return true;
}
