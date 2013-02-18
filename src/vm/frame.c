#include "vm/frame.h"
#include "threads/vaddr.h"


/* Finds and returns the starting index of the first group of CNT
   consecutive empty frame table entries in FT at or after START.
   If there is no such group, returns FRAME_TABLE_ERROR */
size_t
frame_table_scan (struct frame_table *ft, size_t start, size_t cnt)
{
  ASSERT (ft != NULL);
  ASSERT (start <= ft->page_cnt);

  if (cnt > 0 && cnt <= ft->page_cnt)
  {
    size_t i;
    for (i = start; i < ft->page_cnt - cnt + 1; i++)
    {
      if (ft->frames[i] == NULL)
      {
        size_t j;
        for (j = 1; j < cnt; j++)
          if (ft->frames[i + j] != NULL)
            break;
        if (j == cnt)
          return i;
        else
          i = i + j;
      }
    }
  }
  return FRAME_TABLE_ERROR;
}

/* Set the CNT consecutive frame table entries in FT starting at index START
   to the virtual addresses of PTEs pointing to consecutive pages starting
   at VADDR according to page directory PD.
   Create a new page table if not found and CREATE is true. */
void
frame_table_set_multiple (struct frame_table *ft, size_t start, size_t cnt,
                          uint32_t *pd, uint8_t *vaddr, bool create)
{
  ASSERT(pg_ofs (vaddr) == 0);
  uint32_t *pte_addr;
  size_t i;
  for (i = 0; i < cnt; i++)
  {
    pte_addr = lookup_page (pd, vaddr + i * PGSIZE, create);
    ft->frames[start + i] = pte_addr;
  }
}

size_t
frame_table_size (size_t page_cnt)
{
  return page_cnt * sizeof (uint32_t *);
}

void
frame_table_create (struct frame_table *ft, size_t page_cnt, void *block,
    size_t block_size UNUSED)
{
  /* Check if space for FTEs is enough for the pool */
  ASSERT (block_size >= frame_table_size (page_cnt));

  ft->page_cnt = page_cnt;
  ft->frames = (uint32_t **) block;
  memset (ft->frames, 0, page_cnt * sizeof(uint32_t*));
}

/* Returns TRUE if all frame table entries from START to START + CNT are used*/
bool
frame_table_all (const struct frame_table *ft, size_t start, size_t cnt)
{
  size_t i;

  ASSERT (ft != NULL);
  ASSERT (start <= ft->page_cnt);
  ASSERT (start + cnt <= ft->page_cnt);

  for (i = start; i < start + cnt; i++)
    if (ft->frames[i] == NULL)
      return false;
  return true;
}
