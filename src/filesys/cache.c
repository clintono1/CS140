#include "filesys/cache.h"

/* Initialize cache */
void
cache_init(void)
{
  lock_init(&global_cache_lock);
//  lock_init(&io_lock);
  list_init(&evict_wb_list);
  cache_used_num = 0;
  hand = 0;

  int i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
	buffer_cache[i].ever_loaded = false;
	buffer_cache[i].sector_id = 0;
	buffer_cache[i].accessed = false;
	buffer_cache[i].dirty = false;
	buffer_cache[i].loading = false;
	buffer_cache[i].flushing = false;
	buffer_cache[i].AW = 0;
	buffer_cache[i].AR = 0;
	buffer_cache[i].WW = 0;
	buffer_cache[i].WR = 0;
	cond_init(&buffer_cache[i].can_read);
	cond_init(&buffer_cache[i].can_write);
	lock_init(&buffer_cache[i].lock);
	memset(buffer_cache[i].data, 0, BLOCK_SECTOR_SIZE*sizeof(uint8_t));
  }
}

/* See whether there is a hit for sector. If yes, return cache id.
 * Else, return -1 */
static int
is_in_cache(block_sector_t sector)
{
  uint32_t i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
	/* TODO: do I need to acquire lock here?  */
	lock_acquire(&buffer_cache[i].lock);
	if(buffer_cache[i].ever_loaded && buffer_cache[i].sector_id == sector)
	{
	  lock_release(&buffer_cache[i].lock);
	  return i;
	}
	lock_release(&buffer_cache[i].lock);
  }
  return -1;
}

/* TODO: it seems that pintos guide emphasize concurrency rather than
 * consistency, no need to wait on AW, WW, etc */

/* Wait on can_read before read data from cache */
static void
cache_read_wait(uint32_t cache_id, bool hit_flag)
{
  /* if hit, also wait on loading */
  if(hit_flag)
  {
	while(buffer_cache[cache_id].AW + buffer_cache[cache_id].WW >= 1
	  	  ||
	  	  buffer_cache[cache_id].loading)
    {
	  cond_wait(&buffer_cache[cache_id].can_read, &buffer_cache[cache_id].lock);
	}
  }
  /* if miss, no need to wait on loading because itself will load it */
  else
  {
    while(buffer_cache[cache_id].AW + buffer_cache[cache_id].WW >= 1)
	{
	  cond_wait(&buffer_cache[cache_id].can_read, &buffer_cache[cache_id].lock);
	}
  }
  buffer_cache[cache_id].WR--;
  buffer_cache[cache_id].AR++;
  lock_release(&buffer_cache[cache_id].lock);
}

/* After reading from cache, signal other threads to enable writing */
static void
cache_read_signal(uint32_t cache_id, bool hit_flag)
{
  lock_acquire(&buffer_cache[cache_id].lock);
  buffer_cache[cache_id].AR--;
  if(buffer_cache[cache_id].AR == 0 && buffer_cache[cache_id].WW > 0)
  {
    cond_signal(&buffer_cache[cache_id].can_write,
				    &buffer_cache[cache_id].lock);
  }
  if(!hit_flag)
  {
	buffer_cache[cache_id].loading = false;
	cond_signal(&buffer_cache[cache_id].can_read, &buffer_cache[cache_id].lock);
	cond_signal(&buffer_cache[cache_id].can_write,
	  			      &buffer_cache[cache_id].lock);
  }
  lock_release(&buffer_cache[cache_id].lock);
}

/* If there is a hit, copy from cache to buffer */
static void
cache_read_hit(void *buffer, off_t start, off_t length, uint32_t cache_id)
{
  PRINTF("cache_read_hit\n");
  lock_acquire(&buffer_cache[cache_id].lock);
  /*TODO: should this statement be in or outside the while loop */
  buffer_cache[cache_id].WR++;
  /*TODO: should this statement be here or at the end of this function? */
  buffer_cache[cache_id].accessed = true;
  /* release global_cache_lock here */
  lock_release(&global_cache_lock);

  cache_read_wait(cache_id, true);

  memcpy(buffer, buffer_cache[cache_id].data + start,
		        length*sizeof(uint8_t));

  cache_read_signal(cache_id, true);

}

/* If it is a miss, load this sector from disk to cache, then copy to buffer */
static void
cache_read_miss(block_sector_t sector, void *buffer, off_t start, off_t length)
{
  PRINTF("cache_read_miss\n");
  /* if cache is not full */
  if(cache_used_num < BUFFER_CACHE_SIZE)
  {
	uint32_t cache_id = cache_used_num;
	cache_used_num++;
	lock_acquire(&buffer_cache[cache_id].lock);
	buffer_cache[cache_id].ever_loaded = true;
	buffer_cache[cache_id].sector_id = sector;
	/*TODO: should this statement be in or outside the while loop */
	buffer_cache[cache_id].WR++;
	/*TODO: should this statement be here or at the end of this function? */
	buffer_cache[cache_id].accessed = true;
	/* loading should be changed before releasing global_cache_lock */
	buffer_cache[cache_id].loading = true;
	lock_release(&global_cache_lock);

	cache_read_wait(cache_id, false);

	/*TODO: need to check whether this lock is necessary */
	/*TODO: need to check whether loading is useful here */
//	lock_acquire(&io_lock);
	block_read(fs_device, sector, buffer_cache[cache_id].data);
//	lock_release(&io_lock);
	memcpy(buffer, buffer_cache[cache_id].data + start,
			                   length*sizeof(uint8_t));

	cache_read_signal(cache_id, false);
  }
  /* if cache is full, do eviction */
  else
  {
	/* choose a cache entry to evict */
	while(1)
	{
	  lock_acquire(&buffer_cache[hand].lock);
	  /* if current being read or written, skip it */
	  /* TODO: should I consider loading or flushing here? */
	  if(buffer_cache[hand].AW + buffer_cache[hand].AR
	     + buffer_cache[hand].WW + buffer_cache[hand].WR >= 1)
	  {
		hand = (hand + 1) % BUFFER_CACHE_SIZE;
		lock_release(&buffer_cache[hand].lock);
		continue;
	  }
	  /* if accessed recently, skip it */
	  if(buffer_cache[hand].accessed)
	  {
		buffer_cache[hand].accessed = false;
		hand = (hand + 1) % BUFFER_CACHE_SIZE;
		lock_release(&buffer_cache[hand].lock);
		continue;
	  }
	  /* if not accessed recently, evict it
	   * for now, use basic clock algorithm. no second chance */
	  else
	  {
		uint32_t evict_id = hand;
		/*TODO: is this necessary? */
		hand = (hand + 1) % BUFFER_CACHE_SIZE;
		/* release global cache lock here */
		lock_release(&global_cache_lock);
		block_sector_t old_sector = buffer_cache[evict_id].sector_id;
		/* This is very important! Two scenarios during eviction
		 * 1. read/write on old sector: will cause cache miss. Then load from
		 * disk to cache. However, we have io_lock to ensure that the sector is
		 * read from disk after this sector is updated completely. Acquire
		 * io_lock before releasing buffer_cache[cache_id].lock. So when another
		 * thread knows the old sector is missing, current thread has already
		 * acquired the io_lock.
		 * 2. read/write on the new sector: will hit. However, they will wait
		 * until loading to be false */
		buffer_cache[evict_id].sector_id = sector;
	    buffer_cache[evict_id].AR++;
	    buffer_cache[evict_id].loading = true;
	    buffer_cache[evict_id].accessed = true;
//	    lock_acquire(&io_lock);
	    lock_release(&buffer_cache[evict_id].lock);
	    /* if dirty */
		if(buffer_cache[evict_id].dirty)
		{
		  block_write (fs_device, old_sector, buffer_cache[evict_id].data);
		  /* release io_lock so that other processes can read the sector
		   * just written to disk */
//		  lock_release(&io_lock);
//		  lock_acquire(&io_lock);
		}
	    block_read(fs_device, sector, buffer_cache[evict_id].data);
//	    lock_release(&io_lock);
	    memcpy(buffer, buffer_cache[evict_id].data + start,
	      			               length*sizeof(uint8_t));
	    cache_read_signal(evict_id, false);
	  }
	}
  }
}

/* Reads bytes [start, start + length) in sector SECTOR from cache into
 * BUFFER. */
void
cache_read_partial(block_sector_t sector, void *buffer,
		                off_t start, off_t length)
{
  PRINTF("reading cache %d\n", sector);
  lock_acquire(&global_cache_lock);
  int cache_id_hit = is_in_cache(sector);
  if(cache_id_hit != -1)
  {
	cache_read_hit(buffer, start, length, cache_id_hit);
  }
  else
  {
	cache_read_miss(sector, buffer, start, length);
  }
  PRINTF("reading cache %d done\n", sector);
}

/* Reads sector SECTOR from cache into BUFFER. */
void
cache_read( block_sector_t sector, void * buffer)
{
  cache_read_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}

/* Wait on can_read before read data from cache */
static void
cache_write_wait(uint32_t cache_id, bool hit_flag)
{
  /* if hit, also wait on loading */
  if(hit_flag)
  {
	while(buffer_cache[cache_id].AW + buffer_cache[cache_id].AR >= 1
	  	  ||
	  	  buffer_cache[cache_id].loading)
    {
	  cond_wait(&buffer_cache[cache_id].can_write,
			        &buffer_cache[cache_id].lock);
	}
  }
  /* if miss, no need to wait on loading because itself will load it */
  else
  {
    while(buffer_cache[cache_id].AW + buffer_cache[cache_id].AR >= 1)
	{
	  cond_wait(&buffer_cache[cache_id].can_write,
			        &buffer_cache[cache_id].lock);
	}
  }
  buffer_cache[cache_id].WW--;
  buffer_cache[cache_id].AW++;
  lock_release(&buffer_cache[cache_id].lock);
}

/* After writing to cache, signal other threads to enable writing or reading */
static void
cache_write_signal(uint32_t cache_id, bool hit_flag)
{
  lock_acquire(&buffer_cache[cache_id].lock);
  buffer_cache[cache_id].AW--;
  if(buffer_cache[cache_id].WW > 0)
  {
    cond_signal(&buffer_cache[cache_id].can_write,
				    &buffer_cache[cache_id].lock);
  }
  else if(buffer_cache[cache_id].WR > 0)
  {
	cond_broadcast(&buffer_cache[cache_id].can_read,
			          &buffer_cache[cache_id].lock);
  }
  if(!hit_flag)
  {
	buffer_cache[cache_id].loading = false;
	cond_signal(&buffer_cache[cache_id].can_read, &buffer_cache[cache_id].lock);
	cond_signal(&buffer_cache[cache_id].can_write,
	  			    &buffer_cache[cache_id].lock);
  }
  lock_release(&buffer_cache[cache_id].lock);
}


/* If there is a hit, copy from buffer to cache */
static void
cache_write_hit (const void *buffer, off_t start,
		             off_t length, uint32_t cache_id)
{
  lock_acquire(&buffer_cache[cache_id].lock);
  /*TODO: should this statement be in or outside the while loop */
  buffer_cache[cache_id].WW++;
  /*TODO: should this statement be here or at the end of this function? */
  buffer_cache[cache_id].accessed = true;
  buffer_cache[cache_id].dirty = true;
  /* release global_cache_lock here */
  lock_release(&global_cache_lock);
  cache_write_wait(cache_id, true);
  memcpy(buffer_cache[cache_id].data + start, buffer,
		                     length*sizeof(uint8_t));
  cache_write_signal(cache_id, true);
}

/* If it is a miss, load this sector from disk to cache, then copy buffer
 * to cache */
static void
cache_write_miss (block_sector_t sector, const void *buffer,
                  off_t start, off_t length)
{
  /* if cache is not full */
  if(cache_used_num < BUFFER_CACHE_SIZE)
  {
	uint32_t cache_id = cache_used_num;
	cache_used_num++;
	lock_acquire(&buffer_cache[cache_id].lock);
	buffer_cache[cache_id].ever_loaded = true;
	buffer_cache[cache_id].sector_id = sector;
	/*TODO: should this statement be in or outside the while loop */
	buffer_cache[cache_id].WW++;
	/*TODO: should this statement be here or at the end of this function? */
	buffer_cache[cache_id].accessed = true;
	buffer_cache[cache_id].dirty = true;
	/* loading should be changed before releasing global_cache_lock */
	buffer_cache[cache_id].loading = true;
	lock_release(&global_cache_lock);

	cache_write_wait(cache_id, false);

	/*TODO: need to check whether this lock is necessary */
	/*TODO: need to check whether loading is useful here */
//	lock_acquire(&io_lock);
	block_read(fs_device, sector, buffer_cache[cache_id].data);
//	lock_release(&io_lock);
	memcpy(buffer_cache[cache_id].data + start, buffer,
                               length*sizeof(uint8_t));

	cache_write_signal(cache_id, false);
  }
  /* if cache is full, do eviction */
  else
  {
	/* choose a cache entry to evict */
	while(1)
	{
	  lock_acquire(&buffer_cache[hand].lock);
	  /* if current being read or written, skip it */
	  /* TODO: should I consider loading or flushing here? */
	  if(buffer_cache[hand].AW + buffer_cache[hand].AR
	     + buffer_cache[hand].WW + buffer_cache[hand].WR >= 1)
	  {
		hand = (hand + 1) % BUFFER_CACHE_SIZE;
		lock_release(&buffer_cache[hand].lock);
		continue;
	  }
	  /* if accessed recently, skip it */
	  if(buffer_cache[hand].accessed)
	  {
		buffer_cache[hand].accessed = false;
		hand = (hand + 1) % BUFFER_CACHE_SIZE;
		lock_release(&buffer_cache[hand].lock);
		continue;
	  }
	  /* if not accessed recently, evict it
	   * for now, use basic clock algorithm. no second chance */
	  else
	  {
		uint32_t evict_id = hand;
		/*TODO: is this necessary? */
		hand = (hand + 1) % BUFFER_CACHE_SIZE;
		/* release global cache lock here */
		lock_release(&global_cache_lock);
		block_sector_t old_sector = buffer_cache[evict_id].sector_id;
		/* This is very important! Two scenarios during eviction
		 * 1. read/write on old sector: will cause cache miss. Then load from
		 * disk to cache. However, we have io_lock to ensure that the sector is
		 * read from disk after this sector is updated completely. Acquire
		 * io_lock before releasing buffer_cache[cache_id].lock. So when another
		 * thread knows the old sector is missing, current thread has already
		 * acquired the io_lock.
		 * 2. read/write on the new sector: will hit. However, they will wait
		 * until loading to be false */
		buffer_cache[evict_id].sector_id = sector;
	    buffer_cache[evict_id].AW++;
	    buffer_cache[evict_id].loading = true;
	    buffer_cache[evict_id].accessed = true;
	    buffer_cache[evict_id].dirty = true;
//	    lock_acquire(&io_lock);
	    lock_release(&buffer_cache[evict_id].lock);
	    /* if dirty */
		if(buffer_cache[evict_id].dirty)
		{
		  block_write (fs_device, old_sector, buffer_cache[evict_id].data);
		  /* release io_lock so that other processes can read the sector
		   * just written to disk */
//		  lock_release(&io_lock);
//		  lock_acquire(&io_lock);
		}
	    block_read(fs_device, sector, buffer_cache[evict_id].data);
//	    lock_release(&io_lock);
	    memcpy(buffer_cache[evict_id].data + start, buffer,
                                   length*sizeof(uint8_t));
	    cache_write_signal(evict_id, false);
	  }
	}
  }
}

/* Writes BUFFER to bytes [start, start + length) in the cache entry
 * corresponding to sector */
void
cache_write_partial (block_sector_t sector, const void *buffer,
		                 off_t start, off_t length)
{
  PRINTF("writing to cache %d\n", sector);
  lock_acquire (&global_cache_lock);
  int cache_id_hit = is_in_cache (sector);
  if(cache_id_hit != -1)
  {
    cache_write_hit (buffer, start, length, cache_id_hit);
  }
  else
  {
    void *tmp_buffer = malloc (BLOCK_SECTOR_SIZE);
    cache_write_miss (sector, tmp_buffer, start, length);
  }
}

/* Writes BUFFER to the cache entry corresponding to sector. */
void
cache_write ( block_sector_t sector, const void *buffer)
{
  cache_write_partial(sector, buffer, 0, BLOCK_SECTOR_SIZE);
}
