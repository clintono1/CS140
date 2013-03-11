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

/* struct for cache entry */
struct cache_entry
{
  bool ever_loaded;					/* whether the entry has ever been loaded */
  block_sector_t sector_id;			/* sector id */
  bool accessed;					/* whether the entry is recently accessed */
  bool dirty;						/* whether this cache is dirty */
  /* TODO: need to check whether loading is necessary */
  bool loading;						/* whether this cache is being loaded */
  /* TODO: need to check whether flushing is necessary */
  bool flushing;					/* whether this cache is being flushed */
  uint32_t AW;						/* # of processes actively writing */
  uint32_t AR;						/* # of processes actively reading */
  uint32_t WW;						/* # of processes waiting to write */
  uint32_t WR;						/* # of processes waiting to read */
  struct condition can_read;		/* whether this cache can be read now */
  struct condition can_write;		/* whether this cache can be written now */
  struct lock lock;		/* fine grained lock for a single cache */
  uint8_t data[BLOCK_SECTOR_SIZE];	/* data for this sector */
};

typedef struct cache_entry cache_entry_t;

/* global buffer cache */
cache_entry_t buffer_cache[BUFFER_CACHE_SIZE];

/* lock for the whole cache */
struct lock global_cache_lock;

/* indicate whether cache is full */
uint32_t cache_used_num;

/* TODO: need to use this to serialize IO? */
/* lock for I/O between filesys and disk */
//struct lock io_lock;

/* hand for clock algorithm */
uint32_t hand;

/* TODO: list for writing back during eviction */
struct list evict_wb_list;

void cache_init (void);
void cache_read (block_sector_t sector, void * buffer);
void cache_write (block_sector_t sector, const void *buffer);
void cache_read_partial (block_sector_t sector, void *buffer,
                         off_t start, off_t length);
void cache_write_partial (block_sector_t sector, const void *buffer,
                          off_t start, off_t length);
