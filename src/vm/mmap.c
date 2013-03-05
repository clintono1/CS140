#include "vm/mmap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"

extern struct lock global_lock_filesys;

/* Mmap_files hash function */
static unsigned
mmap_files_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct mmap_file * mmf = hash_entry (e, struct mmap_file, elem);
  return hash_bytes (&mmf->mid, sizeof(mmf->mid));
}

/* Mmap_files hash_less function */
static bool
mmap_files_hash_less (const struct hash_elem *a,
    const struct hash_elem *b,
    void *aux UNUSED)
{
  const struct mmap_file * ma = hash_entry (a, struct mmap_file, elem);
  const struct mmap_file * mb = hash_entry (b, struct mmap_file, elem);
  return ma->mid < mb->mid;
}

/* Mmap_files initialization function */
void
mmap_files_init (struct thread *t)
{
  hash_init (&t->mmap_files, mmap_files_hash_func, mmap_files_hash_less, NULL);
  t->mmap_files_num_ever = 0;
}

/* Free a single memory mapped file */
void
mmap_free_file (struct hash_elem *elem, void *aux UNUSED)
{
  struct mmap_file *mmf_ptr = hash_entry (elem, struct mmap_file, elem);
  struct thread *cur = thread_current();
  size_t pg_num = mmf_ptr->num_pages;
  size_t pg_cnt = 0;
  for (pg_cnt = 0; pg_cnt < pg_num; pg_cnt++)
  {
    struct hash_elem * spte_d;
    uint32_t *pte = lookup_page (cur->pagedir, mmf_ptr->upage +
                                 pg_cnt * PGSIZE, false);
    ASSERT (*pte & PTE_M);
    struct suppl_pte *spte = suppl_pt_get_spte (&cur->suppl_pt, pte);

    if (*pte & PTE_P)
    {
      struct lock *frame_lock = get_user_pool_frame_lock (pte);
      lock_acquire (frame_lock);

      if (!(*pte & PTE_P))
        goto release_spte;

      *pte |= PTE_I;
      void * kpage = pte_get_page (*pte);

      if (*pte & PTE_D)
      {
        /* No need to acquire flush lock since other processes will not access
           this page and try to page it in if not present */
        *pte |= PTE_F;
        *pte |= PTE_A;
        *pte &= ~PTE_P;
        invalidate_pagedir (thread_current ()->pagedir);

        lock_acquire (&global_lock_filesys);
        off_t bytes_written;
        if (file_is_writable (spte->file))
        {
          bytes_written = file_write_at (spte->file, kpage,
                                         spte->bytes_read, spte->offset);
          /* Since we cannot change the size of file in project 3
           * the following assertion must be true in project 3*/
          ASSERT (bytes_written >= 0 &&
                  (size_t)bytes_written == spte->bytes_read);
        }
        lock_release (&global_lock_filesys);
      }
      palloc_free_page (kpage);

      lock_release (frame_lock);
    }

release_spte:
    spte_d = hash_delete (&cur->suppl_pt, &spte->elem_hash);
    /* ASSERT that this spte must be in the original suppl_pt */
    ASSERT (spte_d != NULL);
    *pte = 0;
    invalidate_pagedir (thread_current()->pagedir);
    free (spte);
  }

  lock_acquire (&global_lock_filesys);
  file_close (mmf_ptr->file);
  lock_release (&global_lock_filesys);
  free (mmf_ptr);
}

/* Free all memory mapped files in current process */
void
mmap_free_files(struct hash *mmfs)
{
  hash_destroy (mmfs, mmap_free_file);
}
