#include "filesys/cache.h"

void cache_init(void)
{
  int i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
	buffer_cache[i].in_use = false;
	buffer_cache[i].sector_id = 0;
	buffer_cache[i].accessed = false;
	buffer_cache[i].dirty = false;
	buffer_cache[i].pinned = false;
	lock_init(&buffer_cache[i].cache_entry_lock);
	memset(buffer_cache[i].data, 0, BLOCK_SECTOR_SIZE*sizeof(uint8_t));
  }
}

static int is_in_cache(block_sector_t sector)
{
  int i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
	if(buffer_cache[i].in_use && buffer_cache[i].sector_id == sector)
	  return i;
  }
  return -1;
}

static int first_free_cache(void)
{
  int i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
	if(!buffer_cache[i].in_use)
	{
	  return i;
	}
  }
  return -1;
}

/* Reads sector SECTOR into BUFFER, which must
   have room for BLOCK_SECTOR_SIZE bytes. */
void cache_read( block_sector_t sector, void * buffer)
{
//  block_read(fs_device, sector, buffer);
  int cache_id_from = is_in_cache(sector);
  if(cache_id_from != -1)
  {
	memcpy(buffer, buffer_cache[cache_id_from].data,
		   BLOCK_SECTOR_SIZE*sizeof(uint8_t));
  }
  else
  {
	int cache_id_to = first_free_cache();
	ASSERT(cache_id_to != -1);
	block_read(fs_device, sector, buffer_cache[cache_id_to].data);
	memcpy(buffer, buffer_cache[cache_id_to].data,
			   BLOCK_SECTOR_SIZE*sizeof(uint8_t));
  }
}

void cache_write ( block_sector_t sector, const void *buffer)
{
  block_write (fs_device, sector, buffer);
}

void cache_read_partial(block_sector_t sector, void *buffer,
		                off_t start, off_t length)
{
  uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
  if (bounce==NULL)
    PANIC("not enough space for bounce!");
  cache_read (sector, bounce);
  memcpy (buffer, bounce + start, length);
  free(bounce);
}

void cache_write_partial(block_sector_t sector, const void *buffer,
		                 off_t start, off_t length)
{
  uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
  if (start > 0 || length < BLOCK_SECTOR_SIZE - start)
    cache_read(sector, bounce);
  else
    memset (bounce, 0, BLOCK_SECTOR_SIZE);
  memcpy(bounce + start, buffer, length);
  cache_write (sector, bounce);
  free(bounce);
}
