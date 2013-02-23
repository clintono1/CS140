#ifndef VM_PAGE_H_
#define VM_PAGE_H_
#include "lib/kernel/hash.h"
#include "filesys/off_t.h"
#include "lib/debug.h"
#include "filesys/file.h"
#include "threads/thread.h"

void suppl_pt_init (struct hash *suppl_pt);
bool suppl_pt_insert_mmf (struct thread *t, uint32_t *pte,
		struct file *file, off_t offset, size_t read_bytes);

/* Supplemental page table entry */
struct suppl_pte
{
  uint32_t *pte;                  /* Virtual address to the page table entry
                                     in the kernel address space */
  struct file *file;              /* File this page is mapped to */
  bool writable;
  off_t offset;                   /* Offset in the file this page is mapped to*/
  size_t bytes_read;              /* Number of bytes read from the file */
  struct hash_elem elem_hash;     /* Element for supplemental page table */
};


#endif /* vm/page.h */
