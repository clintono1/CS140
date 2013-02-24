#include "vm/mmap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"

extern struct lock global_lock_filesys;

static unsigned
mmap_files_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct mmap_file * mmf = hash_entry (e, struct mmap_file, elem);
  return hash_bytes (&mmf->mid, sizeof(mmf->mid));
}

static bool
mmap_files_hash_less (const struct hash_elem *a,
        const struct hash_elem *b,
        void *aux UNUSED)
{
  const struct mmap_file * ma = hash_entry (a, struct mmap_file, elem);
  const struct mmap_file * mb = hash_entry (b, struct mmap_file, elem);
  return ma->mid < mb->mid;
}

void
mmap_files_init (struct thread *t)
{
  hash_init (&t->mmap_files, mmap_files_hash_func, mmap_files_hash_less, NULL);
  t->mmap_files_num_ever = 0;
}

void
mmap_free_file (struct hash_elem *elem, void *aux UNUSED)
{
  struct mmap_file *mmf_ptr;
  mmf_ptr = hash_entry (elem, struct mmap_file, elem);

  struct thread *t = thread_current();
  /* delete the entries in suppl_pt */
  struct suppl_pte *spte_ptr;
  struct hash_elem *h_elem_spte;
  int pg_num = mmf_ptr->num_pages;
  int pg_cnt = 0;
  for (pg_cnt = 0; pg_cnt < pg_num; pg_cnt++)
  {
    struct suppl_pte temp_spte;
    temp_spte.pte = lookup_page (t->pagedir, mmf_ptr->upage +
                                 pg_cnt * PGSIZE, false);
    h_elem_spte = hash_delete (&t->suppl_pt, &temp_spte.elem_hash);
    spte_ptr = hash_entry (h_elem_spte, struct suppl_pte, elem_hash);
    void * kpage = pte_get_page (*spte_ptr->pte);    
    if (spte_ptr->pte != NULL && (*spte_ptr->pte & PTE_D) != 0)
    {
      lock_acquire (&global_lock_filesys);
      file_write_at (spte_ptr->file, kpage,
                     spte_ptr->bytes_read, spte_ptr->offset);
      lock_release (&global_lock_filesys);
      /* Make this frame available in the frame_table */      
      palloc_free_page(kpage);
      /* Set the previously mapped virtual address to non-present*/
      *spte_ptr->pte = 0;
    }
    
    free(spte_ptr);
  }

  lock_acquire (&global_lock_filesys);
  file_close (mmf_ptr->file);
  lock_release (&global_lock_filesys);
  free (mmf_ptr);
}

void
mmap_free_files(struct hash *mmfs)
{
  hash_destroy (mmfs, mmap_free_file);
}
