#ifndef VM_PAGE_H_
#define VM_PAGE_H_
#include "lib/kernel/hash.h"
#include "filesys/off_t.h"
#include "lib/debug.h"

void suppl_pt_init (struct hash *suppl_pt);

/* Supplemental page table entry */
struct suppl_pte
{
  uint8_t *upage;                        /* Virtual address, used as hash key */
  struct file * file;
  off_t offset_in_file;
  size_t page_read_bytes;
  size_t page_zero_bytes;
  bool writable; 

  struct inode *inode;            /* Inode of the memory mapped file */
  off_t offset;                        /* Offset in the memory mapped file */
  struct hash_elem elem_hash;     /* Element for supplemental page table */
  struct list_elem elem_shared;   /* Element for SPTEs sharing the same page*/
};


#endif /* vm/page.h */
