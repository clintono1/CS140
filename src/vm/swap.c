#include "vm/swap.h"
// TODO
#include "threads/thread.h"

struct swap_table swap_table;

extern struct lock global_lock_filesys;

/* Initialize the swap_table */
void swap_table_init (struct swap_table *swap_table)
{
  lock_init ( &swap_table->lock_bitmap);
  lock_init ( &swap_table->lock_swap);
  swap_table->swap_block = block_get_role ( BLOCK_SWAP );
  block_print_stats ();
  if (swap_table->swap_block)
  {
    int pages_in_swap = 
      block_size (swap_table->swap_block) / SECTORS_PER_PAGE;
    swap_table->bitmap = bitmap_create (pages_in_swap);
    /* False means not occupied */
    bitmap_set_all (swap_table->bitmap, false);
    /* The first frame is reserved for stack growth */
    bitmap_set (swap_table->bitmap, 0, true);
  }
}

/* Allocate one frame in the swap block
 * Returns the swap_frame_number */
size_t
swap_allocate_page ( struct swap_table * swap_table)
{
  lock_acquire (&swap_table->lock_bitmap);
  size_t swap_frame_no = bitmap_scan_and_flip (swap_table->bitmap, 0, 1, false);
  lock_release(&swap_table->lock_bitmap);
  if (swap_frame_no == BITMAP_ERROR)
    PANIC ("out of swap space");
  return swap_frame_no;
}

/* Free the swap page with index SWAP_FRAME_NO in SWAP_TABLE */
void
swap_free (struct swap_table * swap_table, size_t swap_frame_no)
{
  ASSERT (swap_table->swap_block != NULL);
  ASSERT (swap_frame_no < bitmap_size (swap_table->bitmap));
  
  lock_acquire (&swap_table->lock_bitmap);
  bitmap_set (swap_table->bitmap, swap_frame_no, false);
  lock_release(&swap_table->lock_bitmap);
}

/* Read a page of data from SWAP_TABLE with index SWAP_FRAME_NO to the memory
   page at BUF */
void
swap_read (struct swap_table *swap_table, size_t swap_frame_no, uint8_t *buf)
{
  int i;
  ASSERT (bitmap_contains (swap_table->bitmap, swap_frame_no, 1, true));
  lock_acquire (&swap_table->lock_swap);
  /* Each iteration reads in BLOCK_SECTOR_SIZE bytes */
  for (i = 0; i < SECTORS_PER_PAGE; i ++)
  {
    block_read (swap_table->swap_block,
                SECTORS_PER_PAGE * swap_frame_no + i, buf);
    buf += BLOCK_SECTOR_SIZE;
  }
  lock_release (&swap_table->lock_swap);
}

/* Write the memory page BUF to the SWAP_TABLE at index SWAP_FRAME_NO */
void
swap_write (struct swap_table *swap_table, size_t swap_frame_no, uint8_t *buf)
{
  int i;
  ASSERT (bitmap_contains (swap_table->bitmap, swap_frame_no, 1, true));
  lock_acquire (&swap_table->lock_swap);
  /* Each iteration reads in BLOCK_SECTOR_SIZE bytes */
  for (i = 0; i < SECTORS_PER_PAGE; i ++)
  {
    block_write (swap_table->swap_block,
                 SECTORS_PER_PAGE * swap_frame_no + i, buf);
    buf += BLOCK_SECTOR_SIZE;
  }
  lock_release (&swap_table->lock_swap);
}
