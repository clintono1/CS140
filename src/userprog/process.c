#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/mmap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmd_line, void (**eip) (void), void **esp);
void argc_counter(const char*str, int *word_cnt, int *char_cnt);
bool argument_pasing (const char *cmd_line, char **esp);

extern struct lock global_lock_filesys;

#define WRITE_BYTE_4(addr, value) **((int**) addr) = (int)value

void get_first_string (const char * src_str, char *dst_str)
{
  const char *begin = src_str;
  const char *end;
  while (*begin == ' ')
    begin ++;
  end = begin;
  while(*end != ' ' && *end != '\0')
    end ++;
  strlcpy (dst_str, begin, end - begin + 1);
}

/* Starts a new thread running a user program loaded from
   CMD_LINE.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmd_line)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of CMD_LINE.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0, NULL);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, cmd_line, PGSIZE);
  char file_path[MAX_FILE_LENGTH];
  get_first_string(fn_copy, file_path);

  struct load_status ls;
  sema_init (&ls.sema_load, 0);
  ls.load_success = false;
  ls.cmd_line = fn_copy;
  ls.parent_thread = thread_current();

  /* Create a new thread to execute FILE_PATH. */
  tid = thread_create (file_path, PRI_DEFAULT, start_process, &ls);

  /* Wait till the process is loaded */
  sema_down (&ls.sema_load);

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  return ls.load_success ? tid : TID_ERROR;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux)
{
  struct load_status *ls = aux;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (ls->cmd_line, &if_.eip, &if_.esp);

  /* Set load result */
  ls->load_success = success;

  if (success)
  {
    struct thread *t = thread_current();
    /* Deny writes to the executable file */
    char file_path[MAX_FILE_LENGTH];
    get_first_string (ls->cmd_line, file_path);
    t->process_file = filesys_open (file_path);
    file_deny_write (t->process_file);

    /* Set is_kernel to false since it is used by a user process */
    t->is_kernel = false;

    struct exit_status *es = t->exit_status;
    /* No need to check if list_lock is NULL here since the parent process
       must be waiting before the child process is loaded */
    lock_acquire (es->list_lock);
    list_push_back (&ls->parent_thread->child_exit_status, &es->elem);
    lock_release (es->list_lock);
  }

  /* Wake up parent process */
  sema_up (&ls->sema_load);

  /* If load failed, quit. */
  palloc_free_page (ls->cmd_line);
  if (!success)
  {
    /* Set ref_counter to 1 so that it can be collected */
    thread_current()->exit_status->ref_counter = 1;
    thread_exit ();
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid )
{
  struct thread *cur = thread_current();
  struct list_elem *e;
  /* Acquire the lock on the list to prevent any changes when traversing.
     No need to check if list_lock is NULL since the process is waiting for
     the list_lock in its own thread struct */
  lock_acquire (&cur->list_lock);
  for (e  = list_begin (&cur->child_exit_status);
       e != list_end (&cur->child_exit_status);
       e  = list_next (e) )
  {
    struct exit_status *es = list_entry (e, struct exit_status, elem);
    if (es->pid == child_tid && es->ref_counter > 0)
    {
      /* Down the semaphore on wait. If child process has already finished,
         this sema_down should return immediately.
         Here the process may sleep while holding list_lock. But this is not
         a problem since when the parent process is alive, no child process
         should modify the list, i.e. removing their own exit_status.
         Sleep without releasing list_lock avoids the overhead to acquire it
         again for list_remove once wake up. */
      sema_down (&es->sema_wait);

      /* ref_counter must be 1 since child process is dead but parent process
         is still alive. */
      ASSERT(es->ref_counter == 1);
      int exit_value = es->exit_value;

      /* The same process can be only waited once. Thus remove it now. */
      list_remove (&es->elem);
      free (es);
      lock_release (&cur->list_lock);
      return exit_value;
    }
  }
  lock_release (&cur->list_lock);
  /* No child process with child_tid found */
  return -1;
}

void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* should print out the information before we destroy the struct that 
     contains exit_status */
  if (!cur->is_kernel)
    printf ("%s: exit(%d)\n", thread_name(), cur->exit_status->exit_value);
  /* free all memory mapped files */
  mmap_free_files(&cur->mmap_files);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* First step: free the exit_status of terminated children processes
     Order of acquiring locks:
      1. list_lock
      2. counter_lock
  */
  struct list_elem *e;
  /* Lock the list to prevent any changes when traversing.
     No need to check if list_lock is NULL since the process is waiting for
     the list_lock in its own thread struct */
  lock_acquire (&cur->list_lock);
  for (e  = list_begin (&cur->child_exit_status);
       e != list_end (&cur->child_exit_status);
       e  = list_next (e))
  {
    struct exit_status *es = list_entry (e, struct exit_status, elem);
    /* Obtain the counter_lock and decrease the ref_counter */
    lock_acquire (&es->counter_lock);
    /* Sanity check: if the ref_counter is 0, it should have been removed */
    ASSERT (es->ref_counter > 0);
    /* Set list_lock pointer in thread struct to NULL since list_lock will be
       released in the parent thread block */
    es->list_lock = NULL;
    es->ref_counter --;
    bool to_be_freed = false;
    if(es->ref_counter == 0)
    {
      /* list_lock is already held. Remove it directly */
      list_remove (&es->elem);
      to_be_freed = true;
    }
    lock_release (&es->counter_lock);
    if (to_be_freed)
      free (es);
  }
  lock_release (&cur->list_lock);

  /* Second step: try to free the exit_status of the current process */
  lock_acquire (&cur->exit_status->counter_lock);
  ASSERT (cur->exit_status->ref_counter > 0);
  cur->exit_status->ref_counter --;
  if(cur->exit_status->ref_counter == 0)
  {
    /* Although the ordering of acquiring list_lock and counter_lock is reverse
       of that in freeing exit_status of children processes (above), it will not
       lead to deadlock since ref_counter guarantees once it comes to here the
       parent process is dead and no others will try to acquire list_lock
       while holding counter_lock */
    if (cur->exit_status->list_lock != NULL)
      lock_acquire (cur->exit_status->list_lock);
    /* Check whether the element is in a list before removing it
       since it is possible that this element is dangling due to
       process start failure */
    if (cur->exit_status->elem.next != NULL &&
        cur->exit_status->elem.prev != NULL)
      list_remove (&cur->exit_status->elem);
    if (cur->exit_status->list_lock != NULL)
      lock_release (cur->exit_status->list_lock);
    lock_release (&cur->exit_status->counter_lock);
    free(cur->exit_status);
  }
  else
  {
    lock_release (&cur->exit_status->counter_lock);
    sema_up (&cur->exit_status->sema_wait);
  }

  /* Release the locks possibly held by the thread */
  if (lock_held_by_current_thread (&global_lock_filesys))
    lock_release (&global_lock_filesys);

  /* Release file resources hold by current thread */
  if (cur->file_handlers != NULL)
  {
    int fd;
    for (fd = 2; fd < cur->file_handlers_size; fd++)
    {
      if (cur->file_handlers[fd] != NULL)
      {
        lock_acquire (&global_lock_filesys);
        file_close (cur->file_handlers[fd]);
        lock_release (&global_lock_filesys);
      }
    }
    free (cur->file_handlers);
  }

  /* Reenable write to this file */
  if (cur->process_file)
  {
    lock_acquire (&global_lock_filesys);
      file_allow_write (cur->process_file);
      file_close (cur->process_file);
    lock_release (&global_lock_filesys);
  }

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

void
argc_counter(const char*str, int *word_cnt, int *char_cnt)
{
  const char *begin = str;
  int in_word = 0;
  do
  {
    while (*begin == ' ')
      begin ++;
    while (*begin != ' ' && *begin != '\0')
    {
      begin ++;
      (*char_cnt) ++;
      in_word = 1;
    }
    if (in_word)
      (*word_cnt) ++;
    in_word = 0;
  }
  while (*begin != '\0');
}

bool
argument_pasing (const char *cmd_line, char **esp)
{
  int argc = 0;
  int char_cnt = 0;
  int mem_size = 0;
  char * arg_data;
  char **arg_pointer;
  char *token, *save_ptr;
  if (cmd_line == NULL)
    return 0;

  /* First, the number of arguments and the bytes of the chars (including '\0')
     are counted. The stack pointer is moved down by this bytes, this adress is
     recorded as arg_data. The stack pointer is further moved down by
     (argc + 1) * sizeof (char*), and the address is recorded as arg_pointer. */
  argc_counter (cmd_line, &argc, &char_cnt );
  mem_size = char_cnt + argc;

  /* Round up to multiples of 4 Bytes */
  mem_size = ROUND_UP (mem_size, sizeof(int));

  /* Check if the argument size is greater than a page */
  if ( mem_size + (argc + 1) * sizeof(char*) + sizeof(char**) + sizeof(int)
       + sizeof(void *) > PGSIZE )
    return false;

  *esp -= mem_size;
  arg_data = (char *)(*esp);

  *esp -= (argc + 1) * sizeof(char*);
  arg_pointer = (char**)(*esp);

  /* Then we writes the argument string and their address to arg_data and
     arg_pointer respectively. Since both the arguement and the pointer are put
     on the stack from lower address to higher address, we ensured the sequence
     of the elements. */
  for (token = strtok_r ((char*) cmd_line, " ", &save_ptr); token != NULL;
    token = strtok_r (NULL, " ", &save_ptr))
  {
    strlcpy (arg_data, token, strlen(token) + 1);
    *arg_pointer = arg_data;

    arg_data += strlen(token) + 1;
    arg_pointer ++;
  }
  *arg_pointer = 0;

  arg_pointer = (char **) (*esp);

  /* Decrease the stack pointer, write the pointer to the first argument */
  *esp -= sizeof (char **);
  WRITE_BYTE_4 (esp, *esp+4);

  /* Decrease the stack pointer, write argc and return address */
  *esp -= sizeof (int);
  WRITE_BYTE_4 (esp, argc);
  *esp -= sizeof (void*);
  **esp = 0;

  return true;
}


/* Loads an ELF executable from CMD_LINE into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
static bool
load (const char *cmd_line, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto fail;
  process_activate ();

  /* Extract the path to the executable file */
  char file_path[MAX_FILE_LENGTH];
  get_first_string (cmd_line, file_path);
  /* Open executable file. */
  file = filesys_open (file_path);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_path);
      goto fail ;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", cmd_line);
      goto fail;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto fail;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto fail;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto fail;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto fail;
            }
          else
            goto fail;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto fail;

  success = argument_pasing (cmd_line, (char **) esp);
  if (!success)
    goto fail;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  /* Add the executable file to the process file resource list */
  int fd = thread_add_file_handler (thread_current (), file);
  if(fd == -1)
	goto fail;

  return success;

 fail:
  /* We arrive here if the load is failed. */
  file_close (file);
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  file_seek (file, ofs);

  struct thread *cur = thread_current();
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         Read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      uint32_t *pte;
      struct suppl_pte *s_pte;

      s_pte = (struct suppl_pte * ) malloc (sizeof (struct suppl_pte));
      if(s_pte == NULL)
    	return false;
      pte =  lookup_page (cur->pagedir, upage, true);
      *pte |= PTE_M;
      s_pte->pte = pte;
      s_pte->file = file;
      s_pte->offset = ofs;
      s_pte->writable = writable;
      s_pte->flags = 0;
      if (writable == 0)
        s_pte->flags |= SPTE_C;
      else if (page_read_bytes != 0)
        s_pte->flags |= SPTE_DI;                    

      ofs = ofs + (uint32_t) PGSIZE;
      s_pte->bytes_read = page_read_bytes;

      hash_insert (&cur->suppl_pt, &s_pte->elem_hash);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;
  uint8_t *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO, upage);
  if (kpage != NULL) 
  {
    success = install_page (upage, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page (kpage);
    unpin_page (thread_current()->pagedir, upage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
