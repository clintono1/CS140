#ifndef VM_MMAP_H
#define VM_MMAP_H

#include "hash.h"
#include "debug.h"

struct thread;

void mmap_files_init (struct thread *t);
void mmap_free_files (struct hash *mmfs);
void mmap_free_file (struct hash_elem *elem, void *aux UNUSED);

/* Memory mapped file */
struct mmap_file
{
  int mid;
  struct file *file;                  /* File the memory is mapped to */
  uint8_t *upage;                     /* User virtual address of the first
                                         mapping page */
  size_t num_pages;                   /* Number of pages mapped to the file */
  struct hash_elem elem;
};

#endif /* vm/mmap.h */
