#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
/* 128 indexes per sector*/
#define DIRECT_IDX_CNT 123
#define IDX_PER_SECTOR (BLOCK_SECTOR_SIZE/4)
#define CAPACITY_L0    (DIRECT_IDX_CNT*BLOCK_SECTOR_SIZE)
#define CAPACITY_L1    (IDX_PER_SECTOR*BLOCK_SECTOR_SIZE)
#define CAPACITY_L2    (IDX_PER_SECTOR*IDX_PER_SECTOR*BLOCK_SECTOR_SIZE)


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sector;              /* Sector number of disk location.*/
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t idx0[DIRECT_IDX_CNT];     /* Direct index */
    block_sector_t idx1;        /* Single indirect index */
    block_sector_t idx2;       /* Double indirect index*/
  };

struct indirect_block
  {
    block_sector_t idx[IDX_PER_SECTOR];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    off_t length;                       /* File size in bytes. */
    struct lock lock_inode;             /* Inode lock */
  };

//dummy cache, Jinchao, please implement these 4 interfaces in cache.c
static void cache_read( block_sector_t sector, void * buffer)
{
  block_read(fs_device, sector, buffer);
}
static void cache_write ( block_sector_t sector, const void *buffer)
{
  block_write (fs_device, sector, buffer);
}
static void cache_read_partial(block_sector_t sector, void *buffer, off_t start, off_t length)
{
  uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
  if (bounce==NULL)
    PANIC("not enough space for bounce!");
  cache_read (sector, bounce);
  memcpy (buffer, bounce + start, length);
  free(bounce);
}
static void cache_write_partial(block_sector_t sector, const void *buffer, off_t start, off_t length)
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

/* Get sector number from indirect index block at SECTOR */
static block_sector_t
indirect_get_sector (block_sector_t sector, off_t ofs)
{
  struct indirect_block *indirect_block;
  indirect_block = malloc(sizeof *indirect_block);
  if (indirect_block == NULL)
    return -1;
  cache_read(sector, indirect_block);
  block_sector_t sec = indirect_block->idx[ofs];
  free(indirect_block);
  return sec;
}

/* For seek pointer at OFS in a file, return 
   which block is this OFS in the direct index */
static inline off_t
offset_direct (off_t ofs)
{
  ASSERT (ofs < CAPACITY_L0 );
  return ofs / BLOCK_SECTOR_SIZE;
}

/* Return the which block it this OFS in the direct index */
static inline off_t 
offset_indirect (off_t ofs)
{
  ASSERT (ofs >= CAPACITY_L0 );
  ASSERT (ofs < CAPACITY_L0 + CAPACITY_L1 );
  return ( ofs - CAPACITY_L0 ) / BLOCK_SECTOR_SIZE;
}


/* Return the which block it this OFS in the first level of 
   double direct index */
static inline off_t 
offset_double_indirect1 (off_t ofs)
{
  ASSERT (ofs >= CAPACITY_L0 + CAPACITY_L1 );
  ASSERT (ofs < CAPACITY_L0 + CAPACITY_L1 + CAPACITY_L2 );
  return ( ofs - CAPACITY_L0 -CAPACITY_L1) / CAPACITY_L1;
}

/* Return the which block it this OFS in the second level of 
   double direct index */
static inline off_t 
offset_double_indirect2 (off_t ofs)
{
  ASSERT (ofs >= CAPACITY_L0 + CAPACITY_L1 );
  ASSERT (ofs < CAPACITY_L0 + CAPACITY_L1 + CAPACITY_L2 );
  return ( ofs - CAPACITY_L0 ) % CAPACITY_L1 / BLOCK_SECTOR_SIZE;
}

/* Returns the block device sector # that contains byte offset POS
   within INODE. Returns -1 if offset POS is beyond the ~8MB limit */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  if ( pos < CAPACITY_L0 )
  {
    /* See the direct index array, element OFS */
    off_t ofs = offset_direct(pos);
    return inode_disk->idx0[ofs];   
  } 
  else if( pos < CAPACITY_L0 + CAPACITY_L1)
  {
    /* Search the indirect index array*/
    off_t ofs_indirect = offset_indirect(pos);    
    return indirect_get_sector (inode_disk->idx1, ofs_indirect);
  }
  else if (pos < CAPACITY_L0 + CAPACITY_L1 + CAPACITY_L2)
  {
    /* Search the doube indirect index array */
    off_t ofs_indirect = offset_double_indirect1(pos);
    off_t ofs_double_indirect = offset_double_indirect2(pos);
    return indirect_get_sector(indirect_get_sector(inode_disk->idx2, ofs_indirect) 
                               ,ofs_double_indirect);
  }
  else
    return -1;
}

/* Allocate a new zero sector */
static block_sector_t
allocate_zeroed_sector (void)
{
  block_sector_t sector;
  if (!free_map_allocate (1, &sector))
    return -1;
  char *zero = NULL;
  zero = calloc (BLOCK_SECTOR_SIZE, sizeof *zero);
  if (zero == NULL)
    return -1;
  memset(zero, 0, BLOCK_SECTOR_SIZE);
  cache_write(sector, zero);
  free (zero);  
  return sector;
}

/* Allocate a new indirect block, fill the FIRST_ENTRY
   as the first sector number*/
static block_sector_t
allocate_indirect_block (off_t first_entry)
{
  block_sector_t sector;
  if (!free_map_allocate (1, &sector))
    return -1;  
  struct indirect_block *indirect_blk = NULL;
  indirect_blk = malloc ( sizeof *indirect_blk);
  if (indirect_blk == NULL)
    return -1;
  indirect_blk->idx[0] = first_entry;
  cache_write(sector, indirect_blk);
  free(indirect_blk);
  return sector;
}


/* Allocate a new sector for the inode_disk, update length */
static bool                                                  
inode_extend_single(struct inode_disk *inode_disk)
{
  block_sector_t data_sector = allocate_zeroed_sector();
  if ((int)data_sector == -1)
    return false;
  off_t file_offset = inode_disk->length -1;
  inode_disk->length += BLOCK_SECTOR_SIZE;
  /* Case 1: Add a block in the direct level */
  if ( (file_offset + BLOCK_SECTOR_SIZE) < CAPACITY_L0)
  {
    off_t ofs = offset_direct(file_offset + BLOCK_SECTOR_SIZE);
    inode_disk->idx0[ofs] = data_sector;
    return true;
  } 
  /* Case 2: Add a block in the indirect level */
  else if ( (file_offset + BLOCK_SECTOR_SIZE) < CAPACITY_L0 + CAPACITY_L1)
  {
    /* Case 2.1: Need to allocate new indirect block */
    if ( file_offset < CAPACITY_L0 )
    {
      block_sector_t indirect_sector;
      indirect_sector = allocate_indirect_block (data_sector);
      inode_disk->idx1 = indirect_sector;
      return ((int)indirect_sector != -1);
    } 
    /* Case 2.2: No need to allocate new indirect block*/
    else
    {
      off_t ofs = offset_indirect(file_offset + BLOCK_SECTOR_SIZE);
      struct indirect_block *indirect_blk;
      indirect_blk = malloc(sizeof *indirect_blk);
      if (indirect_blk == NULL)
        return false;
      cache_read(inode_disk->idx1, indirect_blk);
      indirect_blk->idx[ofs] =  data_sector;
      cache_write(inode_disk->idx1,indirect_blk);
      free(indirect_blk);
      return true;
    }    
  }
  /* Case 3: Add a block in the double-indirect level */
  else if ( (file_offset + BLOCK_SECTOR_SIZE) < CAPACITY_L0 + CAPACITY_L1 + CAPACITY_L2)
  {
    /* Case 3.1: Need to allocate both indirect and double indirect index block */
    if ( file_offset < CAPACITY_L0 + CAPACITY_L1)
    {
      block_sector_t double_indirect_sector;
      block_sector_t indirect_sector;
      double_indirect_sector = allocate_indirect_block (data_sector);
      indirect_sector = allocate_indirect_block(double_indirect_sector);
      inode_disk->idx2 = indirect_sector;
      return ( (int)indirect_sector != -1 && (int)double_indirect_sector != -1);
    } 
    /* Case 3.2 No need to allocate first level indirect index block */
    else
    {
      off_t ofs1 = offset_double_indirect1 ( file_offset );
      off_t ofs2 = offset_double_indirect1 ( file_offset + BLOCK_SECTOR_SIZE);
      struct indirect_block *indirect_blk;
      indirect_blk = malloc(sizeof *indirect_blk);
      if (indirect_blk == NULL)
        return false;
      cache_read( inode_disk->idx2, indirect_blk);
      /* Case 3.2.1: Need to allocate a double indirect index block */
      if ( ofs1 != ofs2)
      {
        block_sector_t double_indirect_sector;
        double_indirect_sector = allocate_indirect_block (data_sector);
        indirect_blk->idx[ofs2] = double_indirect_sector;
        cache_write(inode_disk->idx2, indirect_blk);
        free(indirect_blk);
        return ((int)double_indirect_sector != -1);
      } 
      /* Case 3.2.2: No need to allocate a double indirect index block */
      else
      {
        off_t ofs_l2 = offset_double_indirect2(file_offset + BLOCK_SECTOR_SIZE);
        struct indirect_block *double_indirect_blk;
        double_indirect_blk = malloc(sizeof *double_indirect_blk);
        if (double_indirect_blk == NULL)
          return false;
        cache_read( indirect_blk->idx[ofs1], double_indirect_blk);
        double_indirect_blk->idx[ofs_l2] = data_sector;
        cache_write(indirect_blk->idx[ofs1], double_indirect_blk);
        free(indirect_blk);
        free(double_indirect_blk);
        return true;
      }
    }
  }
  /* Case 4: exceed max file length, return false */
  else
    return false;
}

/* Extend the length of the file to exactly LENGTH, possibly allocating new blocks*/
static bool 
inode_extend_to_size(struct inode_disk *inode_disk, const off_t length)
{
  off_t extention = length - inode_disk->length;
  off_t cur_left = ROUND_UP(inode_disk->length, BLOCK_SECTOR_SIZE) - inode_disk->length;
  /* Case 1: no need to allocate new sectors */
  if (extention <= cur_left)
  {
    inode_disk->length = length;
    return true;
  }
  else
  {
    inode_disk->length = ROUND_UP(inode_disk->length, BLOCK_SECTOR_SIZE);
    while ( inode_disk->length < length )
    {
      if (!inode_extend_single(inode_disk))
        return false;
    }
    inode_disk->length = length;
    return true;
  }

}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
//
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */

bool
inode_create (block_sector_t sector, off_t length)  
{
  struct inode_disk *disk_inode = NULL;
  ASSERT (length >= 0);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT ((sizeof *disk_inode) == BLOCK_SECTOR_SIZE);

  
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL)
    return false;
  disk_inode->length = 0;
  inode_extend_to_size( disk_inode, length);
  ASSERT(disk_inode->length >= length);
  ASSERT(disk_inode->length-length < BLOCK_SECTOR_SIZE);
  disk_inode->magic = INODE_MAGIC;
  disk_inode->sector = sector;
  cache_write(sector, disk_inode);
  free (disk_inode);
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Not opened yet. Allocate memory */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock_inode);

  struct inode_disk *inode_dsk;
  // TODO: whether need inode->length??
  inode_dsk = malloc(sizeof *inode_dsk);
  if (inode_dsk == NULL)
    return NULL;
  cache_read ( sector, inode_dsk);
  inode->length = inode_dsk->length;
  free(inode_dsk);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire(&inode->lock_inode);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          /* remove the on-disk inode and all the sectors it points to */
          struct inode_disk *inode_dsk;
          inode_dsk = malloc(sizeof *inode_dsk);
          if (inode_dsk == NULL)
            PANIC(" couldn't allocate inode_disk!");
          cache_read(inode->sector, inode_dsk);
          off_t file_end = ROUND_UP(inode->length, BLOCK_SECTOR_SIZE);
          off_t ofs;
          block_sector_t sector;
          for ( ofs = 0; ofs < file_end; ofs += BLOCK_SECTOR_SIZE)
          {
            sector = byte_to_sector( inode_dsk, ofs);
            free_map_release(sector, 1);
          }
          free(inode_dsk);
          free_map_release (inode->sector, 1);
        }
      /* remove the in-memory inode if open_cnt = 0*/
      lock_release(&inode->lock_inode);
      free (inode); 
      return;
    }
  lock_release(&inode->lock_inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  lock_acquire(&inode->lock_inode);
  ASSERT (inode != NULL);
  inode->removed = true;
  lock_release(&inode->lock_inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */

// 从inode指向的文件读入，读到buffer里，读size这么多个byte，从offset个byte开始读。offset每轮都要前进，可以理解为每轮的起点

off_t         
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  struct inode_disk *inode_dsk;
  inode_dsk = malloc(sizeof *inode_dsk);
  if (inode_dsk == NULL)
    return 0;
  cache_read(inode->sector, inode_dsk);
  /* Acquire the lock and then read the file length */
  lock_acquire(&inode->lock_inode);
  size_t total_length = inode_length(inode);
  lock_release(&inode->lock_inode);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_dsk, offset);
//TODO:
      //printf("  offset=%d, at sector %d\n", (int)offset, (int)sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = total_length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector and partially copy into caller's buffer */
          cache_read_partial(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free(inode_dsk);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct inode_disk *inode_dsk;
  inode_dsk = malloc(sizeof *inode_dsk);
  if (inode_dsk == NULL)
    return 0;
  cache_read(inode->sector, inode_dsk);

  if (inode->deny_write_cnt)
    return 0;
  /* If total bytes to be written is larger than current file length,
     need to extend the file to (offset + size). Don't release the lock
     until finish extendingly writing the file*/
  if ( offset + size > inode->length )
  {
    lock_acquire(&inode->lock_inode);
    inode_extend_to_size(inode_dsk, offset + size);
    inode->length = inode_dsk->length;
  }
  //printf("offset = %d, size = %d, file size = %d \n", offset, size, inode->length);
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_dsk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      //TODO: implement write past end of file! need acquire a lock and then write past eof
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          cache_write_partial(sector_idx, buffer + bytes_written, sector_ofs, chunk_size );
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  if (lock_held_by_current_thread(&inode->lock_inode))
    lock_release(&inode->lock_inode);
  ASSERT(inode->sector == inode_dsk->sector);
  cache_write(inode->sector, inode_dsk);
  free(inode_dsk);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->lock_inode);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lock_inode);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&inode->lock_inode);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lock_inode);
}

/* Returns the length, in bytes, of INODE's data. */

off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}
