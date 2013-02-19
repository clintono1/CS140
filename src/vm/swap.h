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
  struct block *swap_block;
  struct bitmap *bitmap;
  struct lock lock;
};


void swap_table_init (struct swap_table *);
size_t swap_allocate_page ( struct swap_table *);
void swap_free (struct swap_table *, size_t);
void swap_read (struct swap_table *, size_t, char *);
void swap_write (struct swap_table *, size_t, char *) ;
