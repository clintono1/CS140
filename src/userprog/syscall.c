#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "lib/string.h"
#include "lib/user/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/mmap.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/input.h"
#include "threads/pte.h"

static void syscall_handler (struct intr_frame *);
static inline bool valid_vaddr_range(const void * vaddr, unsigned size);
static bool preload_user_memory (const void *vaddr, size_t size,
                                 bool allocate, uint8_t *esp);
static bool unpin_user_memory (uint32_t *pd, const void *vaddr, size_t size);
static void syscall_handler (struct intr_frame *);
static inline bool valid_vaddr_range(const void * vaddr, unsigned size);

#ifdef EXPLICIT_MEM_CHECK
static bool check_user_memory (const void *vaddr, size_t size, bool to_be_written);
#endif

void  _exit (int status);
static void  _halt (void);
static pid_t _exec (const char *cmd_line, uint8_t *esp);
static int   _wait (pid_t pid);
static bool  _create (const char *file, unsigned initial_size, uint8_t *esp);
static bool  _remove (const char *file, uint8_t *esp);
static int   _open (const char *file, uint8_t *esp);
static int   _filesize (int fd);
static int   _read (int fd, void *buffer, unsigned size, uint8_t *esp);
static int   _write (int fd, const void *buffer, unsigned size, uint8_t *esp);
static void  _seek (int fd, unsigned position);
static unsigned _tell (int fd);
static void  _close (int fd);
static mapid_t  _mmap (int fd, void *addr);
static void  _munmap (mapid_t mapping);
static bool _chdir (const char *dir);
static bool _mkdir (const char *dir);
static bool _readdir (int fd, char *name);
static bool _isdir (int fd);
static int  _inumber (int fd);

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
    _exit (-1);

  return *(esp + offset);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{

  /* Convert ESP to a int pointer */
  int * esp = (int *)f->esp;
  uint32_t arg1, arg2, arg3;

  if ( !valid_vaddr_range(esp, 0) )
    _exit (-1);

  struct thread *t = thread_current();
  t->in_syscall = true;

  ASSERT (t->esp == NULL);
  t->esp = f->esp;

  /* Get syscall number */
  int syscall_no = *esp;

  switch(syscall_no)
  {
    case SYS_HALT:
      _halt ();
      break;

    case SYS_EXIT:
      arg1 = get_argument (esp, 1);
      _exit (arg1);
      break;

    case SYS_EXEC:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _exec ((char*) arg1, f->esp);
      break;

    case SYS_WAIT:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _wait ((int) arg1);
      break;

    case SYS_CREATE:
      arg1 = get_argument (esp, 1);
      arg2 = get_argument (esp, 2);
      f->eax = (uint32_t) _create ((const char*)arg1, (unsigned)arg2, f->esp);
      break;

    case SYS_REMOVE:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _remove ((const char*)arg1, f->esp);
      break;

    case SYS_OPEN:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _open ((const char*)arg1, f->esp);
      break;

    case SYS_FILESIZE:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _filesize ((int)arg1);
      break;

    case SYS_READ:
      arg1 = get_argument (esp, 1);
      arg2 = get_argument (esp, 2);
      arg3 = get_argument (esp, 3);
      f->eax = (uint32_t) _read ((int)arg1, (void*)arg2, arg3, f->esp);
      break;

    case SYS_WRITE:
      arg1 = get_argument (esp, 1);
      arg2 = get_argument (esp, 2);
      arg3 = get_argument (esp, 3);
      f->eax = (uint32_t) _write ((int)arg1, (const void*)arg2, arg3, f->esp);
      break;

    case SYS_SEEK:
      arg1 = get_argument (esp, 1);
      arg2 = get_argument (esp, 2);
      _seek ((int)arg1, (unsigned)arg2);
      break;

    case SYS_TELL:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _tell ((int)arg1);
      break;

    case SYS_CLOSE:
      arg1 = get_argument (esp, 1);
      _close ((int)arg1);
      break;

    case SYS_MMAP:
      arg1 = get_argument (esp, 1);
      arg2 = get_argument (esp, 2);
      f->eax = _mmap ((int)arg1, (void *)arg2);
      break;

    case SYS_MUNMAP:
      arg1 = get_argument (esp, 1);
      _munmap ((mapid_t)arg1);
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

  ASSERT (t->esp != NULL);
  t->esp = NULL;
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

/* Part1: syscalls for process control */
static void
_halt (void)
{
  shutdown_power_off ();
}

static pid_t
_exec (const char *cmd_line, uint8_t *esp)
{
  /* Check address */
  if (!valid_vaddr_range (cmd_line, 0))
    _exit (-1);

  if (!valid_vaddr_range (cmd_line, strlen(cmd_line)))
    _exit (-1);

  char file_path[MAX_FILE_LENGTH];
  get_first_string (cmd_line, file_path);

  if (!preload_user_memory (cmd_line, strlen (cmd_line), false, esp))
  {
    _exit (-1);
  }  
  /* Lock on process_execute since it needs to open the executable file */
  struct file *file = filesys_open (file_path);
  if (file == NULL ) 
  { 
     return -1; 
  }
  pid_t pid = (pid_t) process_execute (cmd_line);
  unpin_user_memory (thread_current()->pagedir, cmd_line, strlen (cmd_line));
  if (pid == (pid_t) TID_ERROR)
    return -1;
  return pid;
}

void
_exit (int status)
{
  struct thread * cur_thread = thread_current ();
  cur_thread->exit_status->exit_value = status;
  thread_exit ();
}

static int
_wait (pid_t pid)
{
  return process_wait (pid);
}


/* Part2: syscalls for file system */
static bool
_create (const char *file, unsigned initial_size, uint8_t *esp)
{
  
  if (!valid_vaddr_range (file, 0))
    _exit (-1);

  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  if (!preload_user_memory (file, strlen (file), false, esp))
    _exit (-1);

  bool success = filesys_create (file, initial_size);
  unpin_user_memory (thread_current()->pagedir, file, strlen (file));
  return success;
}

static bool
_remove (const char *file, uint8_t *esp)
{
  if (!valid_vaddr_range (file, 0))
    _exit (-1);

  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  if (!preload_user_memory (file, strlen (file), false, esp))
    _exit (-1);

  bool success = filesys_remove (file);

  unpin_user_memory (thread_current()->pagedir, file, strlen (file));
  return success;
}

static int
_open (const char *file, uint8_t *esp)
{
  if (!valid_vaddr_range (file, 0))
    _exit (-1);
  if (!valid_vaddr_range (file, strlen (file)))
    _exit (-1);

  if (!preload_user_memory (file, strlen (file), false, esp))
    _exit (-1);

  struct file *f = filesys_open (file);
  unpin_user_memory (thread_current()->pagedir, file, strlen (file));

  if (f == NULL)
    return -1;

  return thread_add_file_handler (thread_current(), f);
}

static int
_filesize (int fd)
{
  struct thread* t = thread_current ();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    _exit (-1);

  int size = (int) file_length (t->file_handlers[fd]);

  return size;
}

static int
_read (int fd, void *buffer, unsigned size, uint8_t *esp)
{
  if (!valid_vaddr_range (buffer, size))
     _exit (-1);

#ifdef EXPLICIT_MEM_CHECK
  if (!check_user_memory (buffer, size, true))
    _exit (-1);
#endif

  if (fd == STDOUT_FILENO)
    return -1;

  if (!preload_user_memory (buffer, size, true, esp))
    _exit (-1);

  /* Verify whether the buffer to read data to is writable */
  void *upage = pg_round_down (buffer);
  while (upage < buffer + size)
  {
    uint32_t *pte = lookup_page (thread_current()->pagedir, upage, false);
    ASSERT (pte != NULL);
    if (!(*pte & PTE_W))
      _exit (-1);
    upage += PGSIZE;
  }

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
      unpin_user_memory (t->pagedir, buffer, size);
      return result;
  }
  else if (valid_file_handler (t, fd))
  {
      struct file *file = t->file_handlers[fd];
      result = file_read (file, buffer, size);
      unpin_user_memory (t->pagedir, buffer, size);
      return result;
  }
  return -1;
}

static int
_write (int fd, const void *buffer, unsigned size, uint8_t *esp)
{
  if (!valid_vaddr_range (buffer, size))
    _exit (-1);

#ifdef EXPLICIT_MEM_CHECK
  if (!check_user_memory (buffer, size, false))
    _exit (-1);
#endif

  if (size <= 0)
    return 0;

  if (fd == STDIN_FILENO)
    return -1;

  if (!preload_user_memory (buffer, size, false, esp))
    _exit (-1);

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
  unpin_user_memory (t->pagedir, buffer, size);
  return result;
}

static void
_seek (int fd, unsigned position)
{
  struct thread *t = thread_current ();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

   file_seek (file, position);

}

static unsigned
_tell (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

  off_t n= file_tell (file);
  return n;
}

static void
_close (int fd)
{
  struct thread* t = thread_current();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    return;

  file_close (t->file_handlers[fd]);

  thread_remove_file_handler (t, fd);
}

/* Map system call : map a file to memory */
static mapid_t
_mmap (int fd, void *addr)
{
  struct thread *t = thread_current();

  /* check whether fd and addr are valid */
  if (!valid_vaddr_range(addr, PGSIZE-1) || addr ==0x0 ||
    pg_ofs(addr) != 0 || !valid_file_handler(t, fd))
    return MAP_FAILED;

  /* check if file length is larger than 0 */
  int len = _filesize(fd);
  if(len <= 0)
    return MAP_FAILED;

  /* check whether this new file mapping overlaps any existing segment */
  int offset = 0;
  for(offset = 0; offset < len; offset += PGSIZE)
  {
    if(lookup_page(t->pagedir, addr + offset, false))
      return MAP_FAILED;
  }

  struct file *file_to_map = file_reopen (t->file_handlers[fd]);

  if(!file_to_map)
    return MAP_FAILED;

  struct mmap_file *mf;
  mf = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  if(mf == NULL)
    return MAP_FAILED;
  mf->mid = t->mmap_files_num_ever;
  t->mmap_files_num_ever++;
  mf->file = file_to_map;
  mf->upage = addr;

  /* Fill in entries in supplementary table and page table */
  offset = 0;
  int pg_num = 0;
  size_t read_bytes = PGSIZE;
  uint32_t *pte;
  uint32_t *pd = t->pagedir;
  for (offset = 0; offset < len; offset += PGSIZE)
  {
    /* Fill in page table entry in pagedir, but do not allocate memory */
    ASSERT (pagedir_get_page (pd, addr + offset) == NULL);
    pte = lookup_page (pd, addr + offset, true);
    ASSERT (pte != NULL);
    ASSERT ((*pte & PTE_P) == 0);
    *pte = PTE_U | PTE_M;
    bool is_writable = file_is_writable(t->file_handlers[fd]);
    *pte |= is_writable ? PTE_W : ~PTE_W;
    /* Create suppl_pte */
    if(offset + PGSIZE >= len)
      read_bytes = len - offset;
    if (!suppl_pt_insert_mmf (t, pte, is_writable, file_to_map, offset, read_bytes))
      return MAP_FAILED;
    pg_num ++;
  }
  mf->num_pages = pg_num;
  if( hash_insert (&t->mmap_files, &mf->elem) != NULL)
    return MAP_FAILED;

  return mf->mid;
}

/* Unmap system call : unmap a file */
static void
_munmap(mapid_t mapping)
{
  struct thread *t = thread_current();
  struct mmap_file mf;
  struct hash_elem *h_elem_mf;
  mf.mid = mapping;
  h_elem_mf = hash_delete (&t->mmap_files,&mf.elem);
  /* otherwise, such mapping not exists */
  if(h_elem_mf)
    mmap_free_file (h_elem_mf, NULL);
}

static void
load_page (uint32_t *pte, void *upage)
{
  if ((*pte & PTE_M) == 0)
  {
    load_page_from_swap (pte, upage, true);
  }
  else
  {
    struct suppl_pte *spte;
    spte = suppl_pt_get_spte (&thread_current()->suppl_pt, pte);
    load_page_from_file (spte, upage, true);
  }
  ASSERT (*pte & PTE_I);
}

/* Preload user memory pages between VADDR and VADDR + SIZE.
   If ALLOCATE is true, allocate a new memory page if not found. */
static bool
preload_user_memory (const void *vaddr, size_t size, bool allocate, uint8_t *esp)
{
  if (!valid_vaddr_range (vaddr, size))
    return false;

  void *upage = pg_round_down (vaddr);

  while (upage < vaddr + size)
  {
    uint32_t *pte = lookup_page (thread_current()->pagedir, upage, allocate);
    if (pte == NULL || *pte == 0)
    {
      if (!allocate && pte == NULL)
        return false;
      /* If the accessing page is not on stack, return false.
         Note: when to support malloc in user programs, it is also valid
         if [vaddr, vaddr+size) is in the allocated VA space. */
      if (upage < (void*) esp)
        return false;
      uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO, upage);
      if (kpage != NULL)
      {
        if (!install_page (upage, kpage, true))
        {
          palloc_free_page (kpage);
          return false;
        }
      }
      else
        return false;
      ASSERT (*pte & PTE_I);
    }
    else if ((*pte & PTE_P) == 0)
    {
      load_page (pte, upage);
    }
    else
    {
      struct lock *frame_lock = get_user_pool_frame_lock (pte);
      lock_acquire (frame_lock);
      *pte |= PTE_I;
      if (!(*pte & PTE_P))
      {
        lock_release (frame_lock);
        load_page (pte, upage);
      }
      lock_release (frame_lock);
    }
    upage += PGSIZE;
  }
  return true;
}

/* Unpins user memory pages between VADDR and VADDR + SIZE. */
static bool
unpin_user_memory (uint32_t *pd, const void *vaddr, size_t size)
{
  if (!valid_vaddr_range (vaddr, size))
    return false;

  void *upage = pg_round_down (vaddr);

  while (upage < vaddr + size)
  {
    if (!unpin_page (pd, upage))
      return false;
    upage += PGSIZE;
  }
  return true;
}

/* Part3: syscalls for sub-directories */
static bool
_chdir (const char *name)
{
  if (!valid_vaddr_range (name, 0))
    _exit (-1);
  if (!valid_vaddr_range (name, strlen (name)))
    _exit (-1);

  struct thread *cur = thread_current ();
  if (!strcmp(name, "/"))
  {
    cur->cwd = dir_open_root ();
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
  dir_close (cur->cwd);
  cur->cwd = dir_open (inode);
  return true;
}

static bool
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

static bool
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

static bool
_isdir (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];
  
  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);
  bool is_dir = (inode_is_dir(file_get_inode(file)));
  return is_dir;
}

static int
_inumber (int fd)
{
  struct thread *t  = thread_current();
  struct file *file = t->file_handlers[fd];

  if ( !valid_file_handler (t, fd) || fd < 2)
    _exit(-1);

  int inumber = (inode_get_inumber(file_get_inode(file)));
  return inumber;
}

#ifdef EXPLICIT_MEM_CHECK
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
#endif
