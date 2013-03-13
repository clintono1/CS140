#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdio.h>
#include "devices/block.h"
#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

void cache_init(void);
void cache_read( block_sector_t sector, void * buffer);
void cache_write ( block_sector_t sector, const void *buffer);
void cache_read_partial(block_sector_t sector, void *buffer,
                                off_t start, off_t length);
void cache_write_partial(block_sector_t sector, const void *buffer,
                                off_t start, off_t length);
void cache_readahead(block_sector_t sector);
void cache_flush(void);

#endif /* filesys/cache.h */
