#include "vm/swap.h"

/* Initialize the swap_table */
void swap_table_init (struct swap_table *swap_table)
{
  lock_init ( &swap_table->lock);
  swap_table->swap_block = block_get_role ( BLOCK_SWAP );
  if (swap_table->swap_block)
  {
    int pages_in_swap = 
      block_size (swap_table->swap_block) / SECTORS_PER_PAGE;
    //TODO: can this bitmap be swapped out?
    swap_table->bitmap = bitmap_create (pages_in_swap); 
    /* false means not occupied */
    bitmap_set_all (swap_table->bitmap, false);
  }
}

/* Allocate one page in the swap block */
size_t
swap_allocate_page ( struct swap_table * swap_table)
{
  lock_acquire (&swap_table->lock);
  size_t swap_frame_no = bitmap_scan_and_flip (swap_table->bitmap, 0, 1, false);
  lock_release(&swap_table->lock);
  if (swap_frame_no == BITMAP_ERROR)
    PANIC ("out of swap space");
  return swap_frame_no; 
}

/* Go to the swap block and free the frame number SWAP_FRAME_NO 
    just mark the bitmap as unused */
void
swap_free (struct swap_table * swap_table, size_t swap_frame_no)
{
  ASSERT (swap_table->swap_block != NULL);
  ASSERT (swap_frame_no < bitmap_size (swap_table->bitmap));
  
  lock_acquire (&swap_table->lock);
  bitmap_set (swap_table->bitmap, swap_frame_no, false);
  lock_release(&swap_table->lock);
}


/* Read one page from the swap_block to the memory, the source frame number is 
    SWAP_FRAME_NO and the destination memory address is BUF */
void
swap_read (struct swap_table *swap_table, size_t swap_frame_no, char *buf)
{
  int i;
  /*TODO: should prevent reading from a frame that is un-allocated, add detection later*/
  /* Each loop read in BLOCK_SECTOR_SIZE, that is 512 Bytes */
  for (i = 0; i < SECTORS_PER_PAGE; i ++)
  {
    block_read (swap_table->swap_block,  SECTORS_PER_PAGE * swap_frame_no + i,  buf);
    buf += BLOCK_SECTOR_SIZE;
  }
}


/* write one page from memory to the swap_block, the destination frame number is 
    SWAP_FRAME_NO and the source memory address is BUF */
void
swap_write (struct swap_table *swap_table, size_t swap_frame_no, char *buf)
{
  int i;
  /* each loop read in BLOCK_SECTOR_SIZE, that is 512 Bytes */
  for (i = 0; i < SECTORS_PER_PAGE; i ++)
  {
    block_write (swap_table->swap_block,  SECTORS_PER_PAGE * swap_frame_no + i,  buf);
    buf += BLOCK_SECTOR_SIZE;
  }
}


