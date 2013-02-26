#ifndef VM_PAGE_H
#define VM_PAGE_H
#include "lib/kernel/hash.h"
#include "filesys/off_t.h"
#include "lib/debug.h"
#include "filesys/file.h"
#include "threads/thread.h"

enum spte_flags
{
  SPTE_M  = 0x1,   /* 1 if memory mapped file */
  SPTE_C  = 0x2,   /* 1 if executable's code segment */
  SPTE_DI = 0x4,   /* 1 if initialized data */
  SPTE_DU = 0x8    /* 1 if uninitialized data */
};

void suppl_pt_init (struct hash *suppl_pt);
bool suppl_pt_insert_mmf (struct thread *t, uint32_t *pte, bool is_writable,
                          struct file *file, off_t offset, size_t read_bytes);
struct suppl_pte * suppl_pt_get_spte (struct hash *suppl_pt, uint32_t *pte);

/* Supplemental page table entry */
struct suppl_pte
{
  uint32_t *pte;                  /* Kernel virtual address to the page table entry*/
  struct file *file;              /* File this page is mapped to */
  enum spte_flags flags;          /* Types of the page */
  bool writable;                  /* Whether this page is writable */
  off_t offset;                   /* Offset in the file this page is mapped to*/
  size_t bytes_read;              /* Number of bytes read from the file */
  struct hash_elem elem_hash;     /* Element for supplemental page table */
};

#endif /* vm/page.h */
