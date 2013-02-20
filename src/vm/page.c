#include "page.h"


static unsigned
suppl_pte_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct suppl_pte *spte = hash_entry (e, struct suppl_pte, elem_hash);
  return (int) spte->upage;
}

static bool
suppl_pte_hash_less (const struct hash_elem *a,
                     const struct hash_elem *b,
                     void *aux UNUSED)
{
  struct suppl_pte *spte_a = hash_entry (a, struct suppl_pte, elem_hash);
  struct suppl_pte *spte_b = hash_entry (b, struct suppl_pte, elem_hash);
  return spte_a->upage < spte_b->upage;
}

void
suppl_pt_init (struct hash *suppl_pt)
{
  hash_init (suppl_pt, suppl_pte_hash_func, suppl_pte_hash_less, NULL);
}

bool
suppl_pt_insert_mmf(struct thread *t, uint8_t *start_upage,
		struct file *file, off_t offset, size_t read_bytes)
{
	struct suppl_pte *spte;
	spte = (struct suppl_pte *)malloc(sizeof(struct suppl_pte));
	if(!spte)
	  return false;
	spte->bytes_read = read_bytes;
	spte->file = file;
  /* TODO: is writable? */
	spte->offset = offset;
	spte->upage = start_upage + offset;
	if(!hash_insert(&t->suppl_pt, &spte->elem_hash))
	  return false;

	return true;
}
