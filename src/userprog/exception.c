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
   entry S_PTE and the virtual user address UPAGE */
void
load_page_from_file (struct suppl_pte *s_pte, uint8_t *upage)
{
  printf(" load from file.the helping spte address = %p\n", upage, s_pte);
  uint8_t *kpage = palloc_get_page (PAL_USER | PAL_MMAP, upage);
  if (kpage == NULL)
  {
    printf("load from file fail\n");
    _exit(-1);
  }
  /* If MMF or code or initilized data, Load this page. 
     If uninitialized data, load zero page 
     This is self-explanatory by s_pte->bytes_read and memset zeros*/

  if (file_read_at ( s_pte->file, kpage, s_pte->bytes_read, s_pte->offset)
      != (int) s_pte->bytes_read)
  {
      printf("load from file fail\n");
    palloc_free_page (kpage);
    _exit(-1);
  }

  /* Set the unmapped area to zeros */
  if (PGSIZE - s_pte->bytes_read > 0)
    memset (kpage + s_pte->bytes_read, 0, PGSIZE - s_pte->bytes_read);
  

  /* Add the page to the process's address space. */
  if (!install_page (upage, kpage, s_pte->flags & SPTE_W))
  {
    palloc_free_page (kpage);
      printf("load from file fail\n");
    _exit(-1);
  }
  uint32_t *pte = lookup_page(thread_current()->pagedir,upage, false);
//  printf("after install, pte = %x \n", *pte);
}

void
load_page_from_swap (uint32_t *pte, void *fault_page)
{
    
    // TODO Need to pin the page

    printf(" load from swap: pte = %p\n", fault_page, *pte);
    uint8_t *kpage = palloc_get_page (PAL_USER, fault_page);    
    if (kpage == NULL)
    {
      printf("kpage = null\n");
      _exit (-1);
    }

   size_t swap_frame_no = (*pte & PTE_ADDR)>>PGBITS;
   printf("swap in from frame no = %d\n", swap_frame_no);
  
   if (swap_frame_no == 0 )
   {
     printf("swap_no ==0 \n");
      _exit (-1);
   }

   
   swap_read ( &swap_table, swap_frame_no, kpage);  
   swap_free ( &swap_table, swap_frame_no);
   
   /* Add the page to the process's address space. */
   /* TODO: make sure that data loaded from swap is indeed writable (Song) */
   if (!install_page (fault_page, kpage, true))
   {
     palloc_free_page (kpage);
     _exit (-1);
   }
   printf("load from swap finish\n");
}

static void 
stack_growth( void *fault_page)  
{
  printf("stack growth\n");
  uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO, fault_page);
  if (kpage == NULL)
  {
    printf("stack grow fail\n");
    _exit (-1);
  }
  memset (kpage, 0, PGSIZE);

  if (!install_page (fault_page, kpage, 1))
  {
    palloc_free_page (kpage);
      printf("stack grow fail\n");
    _exit (-1);
  }

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
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  struct thread *cur = thread_current();

  /* If fault in kernel except in system calls, kill the kernel */
  if (!user && !cur->in_syscall)
  {
    /* To implement virtual memory, delete the rest of the function
       body, and replace it with code that brings in the page to
       which fault_addr refers. */
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
     if (!is_user_vaddr (fault_addr))
       _exit(-1);

     uint32_t *pte;
     void *fault_page = pg_round_down (fault_addr);
     /* Find an empty page, fill it with the source indicated by s_ptr,
        map the faulted page to the new allocated frame */
     printf("\nfault page = %p not present:%d, write:%d, user:%d", fault_page, not_present, write, user);
     pte = lookup_page (cur->pagedir, fault_page, false);
     if (pte == NULL)
     {
       printf("!!pte = null\n");
       _exit (-1);
     }

     /* Case 1. Stack growth
        Note: there is a false negative here:
          MOV ..., -4(%esp) will be treated as a stack growth.
          To be really strict, need to check the opcode of the instruction
          pointed by f->eip */
     if (( fault_addr == f->esp - 4  ||    /* PUSH  */
           fault_addr == f->esp - 32 ||
           fault_addr >= f->esp)           /* SUB $n, %esp; MOV ..., m(%esp) */
         && fault_addr >= STACK_BASE
         && (*pte & PTE_ADDR) == 0)
     {
       stack_growth (fault_page);
       return;
     }
    
     /* Case 2. In the swap block*/
     if (not_present && !(*pte & PTE_M))
     {
       load_page_from_swap (pte, fault_page);
       return;
     }

     /* Case 3. In the memory mapped file */
     if (not_present && (*pte & PTE_M))
     {
       struct suppl_pte *s_pte = suppl_pt_get_spte (&cur->suppl_pt, pte);
       load_page_from_file (s_pte, fault_page);
       return;
     }
     
     /* Case 4. Access to an invalid user address or a read-only page */
     printf(" Case 4 exit!\n");
     _exit (-1);
  }
}

