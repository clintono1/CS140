#ifndef VM_PAGE_H
#define VM_PAGE_H
#include "lib/kernel/hash.h"
#include "filesys/off_t.h"
#include "lib/debug.h"
#include "filesys/file.h"
#include "threads/thread.h"


enum spte_flags
{
  SPTE_W = 0x1,           /* 1: Writable 0: Read-only  */
  SPTE_MMF = 0x2,         /* 1: Memory mapped file 0: Executable*/
  SPTE_CODE = 0x4,        /* 1: Executable's Code Segment 
                            0: Executable's Data Segment */
  SPTE_DATA_INI = 0x8,    /* 1: Initialized Data 
                            0: Uninitialized Data */
};


void suppl_pt_init (struct hash *suppl_pt);
bool suppl_pt_insert_mmf (struct thread *t, uint32_t *pte, bool is_writable,
		struct file *file, off_t offset, size_t read_bytes);

/* Supplemental page table entry */
struct suppl_pte
{
  uint32_t *pte;                  /* Kernel virtual address to the page table entry*/
  struct file *file;              /* File this page is mapped to */
  enum spte_flags flags;           /* Flags w.r.t. MMF, Code, Data, Writable */
  off_t offset;                   /* Offset in the file this page is mapped to*/
  size_t bytes_read;              /* Number of bytes read from the file */
  struct hash_elem elem_hash;     /* Element for supplemental page table */
};




#endif /* vm/page.h */
