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

/* Initialize cache */
void cache_init(void);

/* Reads sector SECTOR from cache into BUFFER. */
void cache_read( block_sector_t sector, void * buffer);

/* Writes BUFFER to the cache entry corresponding to the sector. */
void cache_write ( block_sector_t sector, const void *buffer);

/* Reads bytes [start, start + length) in sector SECTOR from cache into
 * BUFFER. */
void cache_read_partial(block_sector_t sector, void *buffer,
                                off_t start, off_t length);

/* Writes BUFFER to bytes [start, start + length) in the cache entry
 * corresponding to the sector */
void cache_write_partial(block_sector_t sector, const void *buffer,
                                off_t start, off_t length);

/* Prefetch interface */
void cache_readahead(block_sector_t sector);

/* write every dirty cache block back to disk */
void cache_flush(void);

#endif /* filesys/cache.h */
