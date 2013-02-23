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

static void syscall_handler (struct intr_frame *);
static inline bool valid_vaddr_range(const void * vaddr, unsigned size);
static bool preload_user_memory (const void *vaddr, size_t size,
                                 bool allocate, uint8_t *esp);

void  _exit (int status);
static void  _halt (void);
static pid_t _exec (const char *cmd_line);
static int   _wait (pid_t pid);
static bool  _create (const char *file, unsigned initial_size);
static bool  _remove (const char *file);
static int   _open (const char *file);
static int   _filesize (int fd);
static int   _read (int fd, void *buffer, unsigned size, uint8_t *esp);
static int   _write (int fd, const void *buffer, unsigned size, uint8_t *esp);
static void  _seek (int fd, unsigned position);
static unsigned _tell (int fd);
static void  _close (int fd);
static mapid_t  _mmap (int fd, void *addr);
static void  _munmap (mapid_t mapping);

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
      f->eax = (uint32_t) _exec ((char*) arg1);
      break;

    case SYS_WAIT:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _wait ((int) arg1);
      break;

    case SYS_CREATE:
      arg1 = get_argument (esp, 1);
      arg2 = get_argument (esp, 2);
      f->eax = (uint32_t) _create ((const char*)arg1, (unsigned)arg2);
      break;

    case SYS_REMOVE:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _remove ((const char*)arg1);
      break;

    case SYS_OPEN:
      arg1 = get_argument (esp, 1);
      f->eax = (uint32_t) _open ((const char*)arg1);
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

/* Part1: syscalls for process control */
static void
_halt (void)
{
  shutdown_power_off ();
}

static pid_t
_exec (const char *cmd_line)
{
  /* Check address */
  if (!valid_vaddr_range (cmd_line, 0))
    _exit (-1);

  if (!valid_vaddr_range (cmd_line, strlen(cmd_line)))
    _exit (-1);
  char file_path[MAX_FILE_LENGTH];
  get_first_string (cmd_line, file_path);
  /* Lock on process_execute since it needs to open the executable file */
  lock_acquire (&global_lock_filesys);
  struct file *file = filesys_open (file_path);
  if (file == NULL ) 
  { 
     lock_release (&global_lock_filesys);
     return -1; 
  }
  pid_t pid = (pid_t) process_execute (cmd_line);
  lock_release (&global_lock_filesys);

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

static bool
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

static int
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

static int
_filesize (int fd)
{
  struct thread* t = thread_current ();
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || !valid_file_handler(t, fd))
    _exit (-1);

  lock_acquire (&global_lock_filesys);
  int size = (int) file_length (t->file_handlers[fd]);
  lock_release (&global_lock_filesys);

  return size;
}

static int
_read (int fd, void *buffer, unsigned size, uint8_t *esp)
{
  if (!valid_vaddr_range (buffer, size))
     _exit (-1);

  if (fd == STDOUT_FILENO)
    return -1;

  if (!preload_user_memory (buffer, size, true, esp))
    _exit (-1);

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
      lock_acquire (&global_lock_filesys );
      result = file_read (file, buffer, size);
      lock_release (&global_lock_filesys);
      return result;
  }
  return -1;
}

static int
_write (int fd, const void *buffer, unsigned size, uint8_t *esp)
{
  if (!valid_vaddr_range (buffer, size))
    _exit (-1);
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
    lock_acquire (&global_lock_filesys);
    result = file_write (file, buffer, size);
    lock_release (&global_lock_filesys);
  }
  return result;
}

static void
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

static unsigned
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

static void
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

  /* check if there is enough virtual memory to map this file
   * should I check supplementary page table too ? not sure
   * whether this is fully correct or not */
  int offset = 0;
  for(offset = 0; offset < len; offset += PGSIZE)
  {
    if(lookup_page(t->pagedir, addr + offset, false))
      return MAP_FAILED;
  }

  lock_acquire (&global_lock_filesys);
  struct file *file_to_map = file_reopen (t->file_handlers[fd]);
  lock_release (&global_lock_filesys);
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

static void
_munmap(mapid_t mapping)
{
  /* delete the entry in mmap_files */
  struct thread *t = thread_current();
  struct mmap_file mf;
  struct hash_elem *h_elem_mf;
  struct mmap_file * mf_ptr;
  mf.mid = mapping;
  h_elem_mf = hash_delete (&t->mmap_files,&mf.elem);
  mf_ptr = hash_entry (h_elem_mf, struct mmap_file, elem);

  // TODO: call free_mmap_file() instead

  /* delete the entries in suppl_pt */
  struct suppl_pte *spte_ptr;
  struct hash_elem *h_elem_spte;
  uint32_t *pd = t->pagedir;
  size_t pg_num = mf_ptr->num_pages;
  size_t pg_cnt = 0;
  for (pg_cnt = 0; pg_cnt < pg_num; pg_cnt++)
  {
    struct suppl_pte temp_spte;
    temp_spte.pte = lookup_page (pd, mf_ptr->upage + pg_cnt * PGSIZE, false);
    h_elem_spte = hash_delete (&t->suppl_pt, &temp_spte.elem_hash);
    spte_ptr = hash_entry (h_elem_spte, struct suppl_pte, elem_hash);
    // TODO: clean frame table
    /* If the page is dirty, write it back to disk */
    if (spte_ptr->pte != NULL && (*spte_ptr->pte & PTE_D) != 0)
    {
      lock_acquire (&global_lock_filesys);
      file_write_at (spte_ptr->file, pte_get_page (*spte_ptr->pte),
                     spte_ptr->bytes_read, spte_ptr->offset);
      lock_release (&global_lock_filesys);
    }
    free (spte_ptr);
  }

  lock_acquire (&global_lock_filesys);
  file_close (mf_ptr->file);
  lock_release (&global_lock_filesys);

  free(mf_ptr);
}

/* Preload user memory page with VADDR.
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
    if (*pte == 0)
    {
      if (!allocate)
        return false;
      /* If the accessing page if not on stack, return false.
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
    }
    else if ((*pte & PTE_P) == 0)
    {
      if ((*pte & PTE_M) == 0)
      {
        load_page_from_swap (pte, upage);
      }
      else
      {
        struct suppl_pte temp;
        temp.pte = pte;
        struct hash_elem *e;
        e = hash_find (&thread_current()->suppl_pt, &temp.elem_hash);
        if ( e == NULL )
          _exit (-1);
        struct suppl_pte *s_pte = hash_entry (e, struct suppl_pte, elem_hash);
        load_page_from_file (s_pte, upage);
      }
    }
    upage += PGSIZE;
  }
  return true;
}
