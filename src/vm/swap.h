#include "devices/block.h"
#include <bitmap.h>
#include <stdio.h>
#include <round.h>
#include <inttypes.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "lib/debug.h"

/* Define how many sectors are there in a page: 4KB / 512B = 8*/
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE) 


struct swap_table
  {
    struct block *swap_block;  /* Swap block used for swap table on the disk */
    struct bitmap *bitmap;     /* Bitmap keeps track of allocation status */
    struct lock lock_bitmap;   /* Lock to synchronize access to the bitmap */
    struct lock lock_swap;     /* Lock to synchronize access to the swap block*/
  };


void swap_table_init (struct swap_table *);
size_t swap_allocate_page ( struct swap_table *);
void swap_free (struct swap_table *, size_t);
void swap_read (struct swap_table *, size_t, uint8_t *);
void swap_write (struct swap_table *, size_t, uint8_t *) ;
