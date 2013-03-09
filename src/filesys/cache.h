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

#define BUFFER_CACHE_SIZE 64

struct cache_entry
{
	bool in_use;
	block_sector_t sector_id;
	bool accessed;
	bool dirty;
	bool pinned;
	struct lock cache_entry_lock;
	uint8_t data[BLOCK_SECTOR_SIZE];
};

typedef struct cache_entry cache_entry_t;

cache_entry_t buffer_cache[BUFFER_CACHE_SIZE];

void cache_init(void);
void cache_read( block_sector_t sector, void * buffer);
void cache_write ( block_sector_t sector, const void *buffer);
void cache_read_partial(block_sector_t sector, void *buffer,
		                off_t start, off_t length);
void cache_write_partial(block_sector_t sector, const void *buffer,
		                off_t start, off_t length);

