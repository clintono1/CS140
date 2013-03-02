#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"
#include "lib/string.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "vm/swap.h"

/* Number of page faults processed. */
static long long page_fault_cnt;
extern struct swap_table swap_table;
extern struct lock swap_flush_lock;
extern struct condition swap_flush_cond;
extern struct lock file_flush_lock;
extern struct condition file_flush_cond;
extern struct lock global_lock_filesys;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
void _exit (int);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

// TODO: clean or simplify
/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
       *Kernel code shouldn't throw exceptions.  (Page faults
       * may cause kernel exceptions--but they shouldn't arrive
       * here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Load data from file into a page according to the supplemental page table
   entry SPTE and the virtual user address UPAGE */
void
load_page_from_file (struct suppl_pte *spte, uint8_t *upage)
{
  bool mmap = (spte->flags & SPTE_M) || (spte->flags & SPTE_C);
  enum palloc_flags flags = PAL_USER | (mmap ? PAL_MMAP : 0);
  uint8_t *kpage = palloc_get_page (flags, upage);
  if (kpage == NULL)
    _exit(-1);

  uint32_t *pte = lookup_page (thread_current()->pagedir, upage, false);
  ASSERT (pte != NULL);

  lock_acquire (&file_flush_lock);
  while (*pte & PTE_F)
  {
    cond_wait (&file_flush_cond, &file_flush_lock);
  }
  lock_release (&file_flush_lock);

  /* If MMF or code or initialized data, Load this page.
     If uninitialized data, load zero page 
     This is self-explanatory by s_pte->bytes_read and memset zeros*/
  off_t bytes_read;
  lock_acquire (&global_lock_filesys);
  bytes_read = file_read_at ( spte->file, kpage, spte->bytes_read, spte->offset);
  lock_release (&global_lock_filesys);

  if ( bytes_read != (int) spte->bytes_read)
  {
    palloc_free_page (kpage);
    _exit(-1);
  }

  /* Set the unmapped area to zeros */
  if (PGSIZE - spte->bytes_read > 0)
    memset (kpage + spte->bytes_read, 0, PGSIZE - spte->bytes_read);

  /* Add the page to the process's address space. */
  if (!install_page (upage, kpage, spte->writable))
  {
    palloc_free_page (kpage);
    _exit(-1);
  }

  /* Set mmap bit to 1 if it is code or mmap file.
     Otherwise 0 since it is uninitialized/initialized data page */
  if (mmap)
  {
    *pte |= PTE_M;
  }
  else
  {
    hash_delete (&thread_current ()->suppl_pt, &spte->elem_hash);
    free (spte);
  }

  // TODO
  printf ("(tid=%d) load_page_from_file %p\n", thread_current ()->tid, upage);

  unpin_pte (pte);
}

/* Load the page pointed by PTE and install the page with the virtual
   address UPAGE */
void
load_page_from_swap (uint32_t *pte, void *page)
{
  ASSERT (pte != NULL);

  uint8_t *kpage = palloc_get_page (PAL_USER, page);
  if (kpage == NULL)
    _exit (-1);

  lock_acquire (&swap_flush_lock);
  while (*pte & PTE_F)
  {
    cond_wait (&swap_flush_cond, &swap_flush_lock);
  }
  lock_release (&swap_flush_lock);

  size_t swap_frame_no = (*pte >> PGBITS);

  if (swap_frame_no == 0 )
    _exit (-1);

  swap_read ( &swap_table, swap_frame_no, kpage);
  swap_free ( &swap_table, swap_frame_no);

  /* Add the page to the process's address space. */
  if (!install_page (page, kpage, true))
  {
    palloc_free_page (kpage);
    _exit (-1);
  }

  unpin_pte (pte);
}

/* Grow the stack at the page with user virtual address UPAGE */
static void
stack_growth( void *upage)
{
  uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO, upage);
  if (kpage == NULL)
    _exit (-1);
  memset (kpage, 0, PGSIZE);

  if (!install_page (upage, kpage, true))
  {
    palloc_free_page (kpage);
    _exit (-1);
  }

  unpin_page (thread_current()->pagedir, upage);
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */
  bool esp_set;      /* True if thread_current->esp is set in this page fault */
  
  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt ++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  struct thread *cur = thread_current();

  /* If fault in kernel except in system calls, kill the kernel */
  if (!user && !cur->in_syscall)
  {
    printf ("Page fault at %p: %s error %s page in %s context.\n",
            fault_addr,
            not_present ? "not present" : "rights violation",
            write ? "writing" : "reading",
            user ? "user" : "kernel");
    kill (f);
  }
  else 
  /* TODO: update comments: If fault in the user program or syscall, should get the info about where to get the page */
  {
     // TODO
     printf ("(tid=%d) page_fault = %p\n", thread_current()->tid, fault_addr);
     if (fault_addr > PHYS_BASE)
       debug_backtrace ();

     if (cur->esp == NULL)
     {
       cur->esp = f->esp;
       esp_set = true;
     }
     else
       esp_set = false;

     if (!is_user_vaddr (fault_addr))
       _exit (-1);

     uint32_t *pte;
     void *fault_page = pg_round_down (fault_addr);
     pte = lookup_page (cur->pagedir, fault_page, false);

     /* Case 1. Stack growth
        Note: there is a false negative here:
          MOV ..., -4(%esp) will be treated as a stack growth.
          To be really strict, need to check the opcode of the instruction
          pointed by f->eip */
     if (( fault_addr == cur->esp - 4  ||  /* PUSH  */
           fault_addr == cur->esp - 32 ||  /* PUSHA */
           fault_addr >= cur->esp)         /* SUB $n, %esp; MOV ..., m(%esp) */
         && fault_addr >= STACK_BASE       /* Stack limit */
         && ((pte == NULL) ||              /* Page is NOT allocated */
             (*pte & PTE_ADDR) == 0))      /* Page is NOT paged out*/
     {
       printf ("(tid=%d) case 1: %p\n", thread_current()->tid, fault_page);
       stack_growth (fault_page);
       goto success;
     }

     // TODO
     if ((unsigned)fault_page <= 0x804a000)
     {
       ASSERT (*pte & PTE_M);
     }

     /* Case 2. In the swap block*/
     if ((pte != NULL) && not_present && !(*pte & PTE_M) && (*pte & PTE_ADDR))
     {
       printf ("(tid=%d) case 2: %p\n", thread_current()->tid, fault_page);
       load_page_from_swap (pte, fault_page);
       goto success;
     }

     /* Case 3. In the memory mapped file */
     if ((pte != NULL) && not_present && (*pte & PTE_M))
     {
       printf ("(tid=%d) case 3: %p\n", thread_current()->tid, fault_page);
       struct suppl_pte *s_pte = suppl_pt_get_spte (&cur->suppl_pt, pte);
       load_page_from_file (s_pte, fault_page);
       goto success;
     }

     /* Case 4. Access to an invalid user address or a read-only page */
     printf ("(tid=%d) case 4: fault_addr=%p pte=%p, *pte=%#x, not_present(%d) write(%d), user(%d)\n",
         thread_current()->tid, fault_addr, pte, (pte != NULL) ? *pte : 0x0,  not_present, write, user);
     debug_backtrace ();
     _exit (-1);

success:
     if (esp_set)
       cur->esp = NULL;
     return;
  }
}

