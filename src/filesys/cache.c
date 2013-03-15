#include "filesys/cache.h"
#include "devices/timer.h"
#include "threads/thread.h"

#define BUFFER_CACHE_SIZE 64
#define WRITE_BEHIND_INTERVAL 30

/* struct for cache entry */
struct cache_entry
{
  block_sector_t sector_id;        /* sector id */
  block_sector_t next_id;          /* id of sector to be loaded if flushing */
  bool accessed;                   /* whether the entry is recently accessed */
  bool dirty;                      /* whether this cache is dirty */
  bool loading;                    /* whether this cache is being loaded */
  bool flushing;                   /* whether this cache is being flushed */
  uint32_t AW;                     /* # of processes actively writing */
  uint32_t AR;                     /* # of processes actively reading */
  uint32_t WW;                     /* # of processes waiting to write */
  uint32_t WR;                     /* # of processes waiting to read */
  struct condition cache_ready;    /* whether this cache can be read/written */
  struct lock lock;                /* fine grained lock for a single cache */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* data for this sector */
};
typedef struct cache_entry cache_entry_t;

/* cache array */
static cache_entry_t buffer_cache[BUFFER_CACHE_SIZE];
/* clock hand for clock algorithm */
static uint32_t hand;
/* global buffer cache */
static struct lock global_cache_lock;

/* read-ahead queue */
static struct list read_ahead_q;
/* read-ahead queue lock */
static struct lock ra_q_lock;
/* read-ahead queue ready condition variable */
static struct condition ra_q_ready;
struct read_a
{
  block_sector_t sector;
  struct list_elem elem;
};
typedef struct read_a read_a_t;


/* Prefetch interface */
void
cache_readahead(block_sector_t sector)
{
  lock_acquire(&ra_q_lock);
  read_a_t * r_ptr = (read_a_t *) malloc(sizeof(read_a_t));
  r_ptr->sector = sector;
  list_push_back(&read_ahead_q, &r_ptr->elem);
  cond_signal(&ra_q_ready, &ra_q_lock);
  lock_release(&ra_q_lock);
}

/* Prefetch Daemon */
static void
cache_readahead_daemon(void * aux UNUSED)
{
  while(true)
  {
	lock_acquire(&ra_q_lock);
	while(list_empty(&read_ahead_q))
	{
      cond_wait(&ra_q_ready, &ra_q_lock);
	}
	struct list_elem * e_ptr = list_pop_front(&read_ahead_q);
	read_a_t * ra_ptr = list_entry(e_ptr, read_a_t, elem);
	block_sector_t sector = ra_ptr->sector;
	free(ra_ptr);
	lock_release(&ra_q_lock);
	void * tmp_buffer = (void *) malloc(BLOCK_SECTOR_SIZE);
	cache_read(sector, tmp_buffer);
	free(tmp_buffer);
  }
}

/* write every dirty cache block back to disk */
void
cache_flush(void)
{
  uint32_t c_ind = 0;
  for(c_ind = 0; c_ind < BUFFER_CACHE_SIZE; c_ind++ )
  {
    lock_acquire(&buffer_cache[c_ind].lock);
    if(buffer_cache[c_ind].dirty)
    {
	  /* wait until this cache block is fully written to disk */
	  if(buffer_cache[c_ind].flushing)
	  {
		lock_release(&buffer_cache[c_ind].lock);
		continue;
	  }
      buffer_cache[c_ind].flushing = true;
      buffer_cache[c_ind].next_id = UINT32_MAX;
      lock_release(&buffer_cache[c_ind].lock);
      block_write(fs_device, buffer_cache[c_ind].sector_id,
                                     buffer_cache[c_ind].data);
      lock_acquire(&buffer_cache[c_ind].lock);
      buffer_cache[c_ind].flushing = false;
      buffer_cache[c_ind].dirty = false;
      cond_signal(&buffer_cache[c_ind].cache_ready,
                              &buffer_cache[c_ind].lock);
      lock_release(&buffer_cache[c_ind].lock);
    }
  }
}

/* Wite-behind function */
static void
write_behind_period(void * aux UNUSED)
{
  while(true)
  {
	timer_sleep(TIMER_FREQ*WRITE_BEHIND_INTERVAL);
	cache_flush();
  }
}

/* Initialize cache */
void
cache_init (void)
{
  hand = 0;
  uint32_t i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    buffer_cache[i].sector_id = UINT32_MAX;
    buffer_cache[i].next_id = UINT32_MAX;
    buffer_cache[i].accessed = false;
    buffer_cache[i].dirty = false;
    buffer_cache[i].loading = false;
    buffer_cache[i].flushing = false;
    buffer_cache[i].AW = 0;
    buffer_cache[i].AR = 0;
    buffer_cache[i].WW = 0;
    buffer_cache[i].WR = 0;
    cond_init(&buffer_cache[i].cache_ready);
    lock_init(&buffer_cache[i].lock);
    memset(buffer_cache[i].data, 0, BLOCK_SECTOR_SIZE*sizeof(uint8_t));
  }
  lock_init(&global_cache_lock);
  list_init(&read_ahead_q);
  lock_init(&ra_q_lock);
  cond_init(&ra_q_ready);
  thread_create ("write_behind_period_t", PRI_DEFAULT,
                           write_behind_period, NULL);
  thread_create ("read_ahead_t", PRI_DEFAULT, cache_readahead_daemon, NULL);
}

/* See whether there is a hit for sector. If yes, return cache id.
 * Else, return -1. */
static int
is_in_cache (block_sector_t sector, bool write_flag)
{
  uint32_t i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    lock_acquire(&buffer_cache[i].lock);
    if(buffer_cache[i].sector_id == sector)
    {
      /* wait until this cache block is fully written to disk */
      while(buffer_cache[i].flushing)
      {
        cond_wait(&buffer_cache[i].cache_ready, &buffer_cache[i].lock);
      }
      /* hit : the block is still in cache after written to disk */
      if(buffer_cache[i].sector_id == sector)
      {
        if(write_flag)
        buffer_cache[i].WW++;
        else
        buffer_cache[i].WR++;
        return i;
      }
      /* miss : the block was being evicted, release lock */
      else
      {
        lock_release(&buffer_cache[i].lock);
        return -1;
      }
    }
    else if(buffer_cache[i].flushing && buffer_cache[i].next_id == sector)
    {
      /* wait until this cache block is fully written to disk */
	  while(buffer_cache[i].flushing)
	  {
		cond_wait(&buffer_cache[i].cache_ready, &buffer_cache[i].lock);
	  }
	  /* hit : the block is still in cache after written to disk */
		if(buffer_cache[i].sector_id == sector)
		{
		  if(write_flag)
		  buffer_cache[i].WW++;
		  else
		  buffer_cache[i].WR++;
		  return i;
		}
		/* miss : the block was being evicted, release lock */
		else
		{
		  lock_release(&buffer_cache[i].lock);
		  return -1;
		}
    }
    lock_release(&buffer_cache[i].lock);
  }
  return -1;
}

/* If the cache is full, find one cache to be evicted using clock algorithm
 * return the pointer of the cache to be evicted */
/* When the cache isn't full, get the very first unused cache entry */
static uint32_t
cache_evict_id (void)
{
  while (1)
  {
    lock_acquire(&buffer_cache[hand].lock);
    /* if the cache block is being used, skip */
    if (buffer_cache[hand].flushing || buffer_cache[hand].loading ||
        buffer_cache[hand].AW + buffer_cache[hand].AR
        + buffer_cache[hand].WW + buffer_cache[hand].WR > 0)
    {
      lock_release(&buffer_cache[hand].lock);
      hand = (hand + 1) % BUFFER_CACHE_SIZE;
      continue;
    }
    /* if it has been accessed recently */
    if (buffer_cache[hand].accessed)
    {
      buffer_cache[hand].accessed = false;
      lock_release(&buffer_cache[hand].lock);
      hand = (hand + 1) % BUFFER_CACHE_SIZE;
    }
    /* if it hasn't been accessed recently, evict it */
    else
    {
      uint32_t result = hand;
      /* release global_cache_lock here, hold find-grained lock */
      lock_release(&global_cache_lock);
      hand = (hand + 1) % BUFFER_CACHE_SIZE;
      return result;
    }
  }
  /* should never reach here */
  ASSERT(1==0);
  return 0;
}

/* Return a cache block for cache_read_miss or cache_write_miss */
static cache_entry_t *
cache_get_entry (block_sector_t sector_id)
{
  uint32_t evict_id = cache_evict_id();
  /* if dirty, write back */
  if (buffer_cache[evict_id].dirty)
  {
    buffer_cache[evict_id].flushing = true;
    buffer_cache[evict_id].next_id = sector_id;
    lock_release(&buffer_cache[evict_id].lock);
    /* IO */
    block_write(fs_device, buffer_cache[evict_id].sector_id,
                               buffer_cache[evict_id].data);
    lock_acquire(&buffer_cache[evict_id].lock);
  }
  /* completely new cache block! */
  buffer_cache[evict_id].dirty = false;
  buffer_cache[evict_id].accessed = false;
  buffer_cache[evict_id].sector_id = sector_id;
  buffer_cache[evict_id].next_id = UINT32_MAX;
  buffer_cache[evict_id].flushing = false;
  /* flush complete, signal */
  cond_signal(&buffer_cache[evict_id].cache_ready,
                    &buffer_cache[evict_id].lock);
  return &buffer_cache[evict_id];
}

/* Reads sector SECTOR from cache into BUFFER. */
void
cache_read ( block_sector_t sector, void * buffer)
{
  cache_read_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

/* Routine for cache read no matter miss or hit */
static void
cache_read_routine (cache_entry_t * cur_c, void *buffer,
                              off_t start, off_t length)
{
  /* multiple reader, single writer to the same block */
  while (cur_c->loading || cur_c->flushing || cur_c->WW + cur_c->AW > 0)
  {
    cond_wait(&cur_c->cache_ready, &cur_c->lock);
  }
  cur_c->WR--;
  cur_c->AR++;
  lock_release(&cur_c->lock);

  /* truly concurrently, multiple reader allowed to the same block */
  memcpy(buffer, cur_c->data + start, length);

  lock_acquire(&cur_c->lock);
  cur_c->AR--;
  cond_signal(&cur_c->cache_ready, &cur_c->lock);
  /* set accessed to true */
  cur_c->accessed = true;
  lock_release(&cur_c->lock);
}

/* If there is a hit, copy from cache to buffer */
static void
cache_read_hit (void *buffer, off_t start, off_t length, uint32_t cache_id)
{
  cache_entry_t *cur_c;
  cur_c = &buffer_cache[cache_id];
  /* release global cache lock */
  lock_release(&global_cache_lock);
  cache_read_routine(cur_c, buffer, start, length);
}

/* If it is a miss, load this sector from disk to cache, then copy to buffer */
static void
cache_read_miss (block_sector_t sector, void *buffer, off_t start, off_t length)
{
  cache_entry_t *cur_c;
  /* get a cache block using eviction */
  cur_c = cache_get_entry(sector);
  /* currently loading cache from disk */
  cur_c->loading = true;
  lock_release(&cur_c->lock);

  /* IO */
  block_read (fs_device, sector, cur_c->data);

  lock_acquire(&cur_c->lock);
  cur_c->loading = false;
  cond_signal(&cur_c->cache_ready, &cur_c->lock);
  cur_c->WR++;
  cache_read_routine(cur_c, buffer, start, length);
}

/* Reads bytes [start, start + length) in sector SECTOR from cache into
 * BUFFER. */
void
cache_read_partial (block_sector_t sector, void *buffer,
                              off_t start, off_t length)
{
  lock_acquire(&global_cache_lock);
  int cache_id_hit = is_in_cache(sector, false);
  /* if hit */
  if(cache_id_hit != -1)
  {
    /* global_cache_lock released in cache_read_hit */
    cache_read_hit(buffer, start, length, cache_id_hit);
  }
  /* if miss */
  else
  {
    /* global_cache_lock released after finding a block to evict */
    cache_read_miss(sector, buffer, start, length);
  }
}

/* Routine for cache write no matter miss or hit */
static void
cache_write_routine (cache_entry_t * cur_c, const void *buffer,
                                     off_t start, off_t length)
{
  /* multiple reader, single writer to the same block */
  while(cur_c->loading || cur_c->flushing || cur_c->AR + cur_c->AW > 0)
  {
    cond_wait(&cur_c->cache_ready, &cur_c->lock);
  }
  cur_c->WW--;
  cur_c->AW++;
  lock_release(&cur_c->lock);

  /* multiple reader, single writer to the same block */
  memcpy(cur_c->data + start, buffer, length);

  lock_acquire(&cur_c->lock);
  cur_c->AW--;
  cond_signal(&cur_c->cache_ready, &cur_c->lock);
  /* set accessed to true */
  cur_c->accessed = true;
  /* set dirty to true */
  cur_c->dirty = true;
  lock_release(&cur_c->lock);
}

/* If there is a hit, copy from buffer to cache */
static void
cache_write_hit (const void *buffer, off_t start,
                         off_t length, uint32_t cache_id)
{
  cache_entry_t *cur_c;
  cur_c = &buffer_cache[cache_id];
  /* release global cache lock  */
  lock_release(&global_cache_lock);
  cache_write_routine(cur_c, buffer, start, length);
}

/* If it is a miss, load this sector from disk to cache, then copy buffer
 * to cache */
static void
cache_write_miss (block_sector_t sector, const void *buffer,
                                  off_t start, off_t length)
{
  cache_entry_t *cur_c;
  /* get a cache block using eviction */
  cur_c = cache_get_entry(sector);
  cur_c->WW++;
  cache_write_routine(cur_c, buffer, start, length);
}

/* Writes BUFFER to the cache entry corresponding to the sector. */
void
cache_write ( block_sector_t sector, const void *buffer)
{
  cache_write_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

/* Writes BUFFER to bytes [start, start + length) in the cache entry
 * corresponding to the sector */
void
cache_write_partial (block_sector_t sector, const void *buffer,
                                     off_t start, off_t length)
{
  lock_acquire(&global_cache_lock);
  int cache_id_hit = is_in_cache (sector, true);
  /* if hit */
  if(cache_id_hit != -1)
  {
    /* global_cache_lock released in cache_write_hit */
    cache_write_hit (buffer, start, length, cache_id_hit);
  }
  /* if miss */
  else
  {
    /* global_cache_lock released after finding a block to evict */
    cache_write_miss (sector, buffer, start, length);
  }
}
