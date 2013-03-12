#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "debug.h"

/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
    bool is_dir;                        /* Directory or file? */
  };

static bool dir_empty (struct inode * inode);
static bool lookup (const struct dir *dir, const char *name,
                    struct dir_entry *ep, off_t *ofsp);

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *  
dir_open (struct inode *inode)
{
  ASSERT (inode_is_dir(inode));
  struct dir *dir = calloc (1, sizeof *dir);
  if (dir != NULL)
  {
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  }
  else
  {
    inode_close (inode);
    free (dir);
    return NULL;
  }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens the current thread's working directory */
struct dir*
dir_open_current(void)
{
  return dir_open (inode_open (thread_current()->cwd_sector));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
  {
    PRINTF("dir%p inode(%d) closed\n", dir, inode_get_inumber(dir->inode));
    inode_close (dir->inode);
    free (dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  if (dir == NULL || name == NULL)
    return false;

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  if (dir == NULL || name == NULL)
    return false;

  dir_lock (dir->inode);
  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;
  dir_unlock (dir->inode);

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR, if the added file is a directory then IS_DIR is true
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name,
         block_sector_t inode_sector, bool is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  if (dir == NULL || name == NULL)
    return false;
  
  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
  {
    //TODO:
    //printf("too long!\n");
    return false;
  }

  dir_lock (dir->inode);

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  e.is_dir = is_dir;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  //printf("added name:%s\n", e.name);
done:
  dir_unlock (dir->inode);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  if (dir == NULL || name == NULL)
  {
    return false;
  }

  dir_lock (dir->inode);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
  {
    goto done;
  }

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
  {
    goto done;
  }
  if (e.is_dir) 
  {
    ASSERT (inode_is_dir(inode));
    /* Can't remove opened inode */
    if (inode_open_cnt (inode) > 1) 
    {
      //printf("/* open_cnt=%d, Can't remove opened inode */\n", inode_open_cnt(inode));
      goto done;
    }
    /* Can't close a non-empty directory */
    if (!dir_empty (inode))
    {
      //printf("/* Can't close a non-empty directory */ \n");
      goto done;
    }
    /* Can't remove cwd */
    if (inode_get_inumber (inode) == thread_current ()->cwd_sector)
    {
      //printf("/* Can't remove cwd */\n");
      goto done;
    }
  } 

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode); 
  success = true;

done:
  dir_unlock (dir->inode);
  inode_close (inode); 
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  if (dir == NULL || name == NULL)
    return false;

  dir_lock (dir->inode);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
  {
    dir->pos += sizeof e;
    //TODO:
    //printf("e.name=%s  in_use(%d)\n", e.name, e.in_use);
    if (e.in_use && strcmp(e.name, ".") && strcmp(e.name, ".."))
    {
      //printf("INSIDE: e.name=%s, in_use(%d)\n", e.name, e.in_use);
      strlcpy (name, e.name, NAME_MAX + 1);
      dir_unlock (dir->inode);
      return true;
    }
  }
  dir_unlock (dir->inode);

  return false;
}

/* Check if an directory's inode is empty */
static bool
dir_empty (struct inode * inode)
{
  ASSERT (inode_is_dir(inode));
  struct dir_entry e;
  size_t ofs;
  for (ofs = 0;
      inode_read_at (inode, &e, sizeof e, ofs) == sizeof e;
      ofs += sizeof e)
  {
    //printf("remain: %s, in_use(%d)\n " , e.name, e.in_use);
    if (e.in_use && strcmp (".", e.name) && strcmp("..", e.name))
    {
      //printf("inuse: remain: %s, in_use(%d)\n " , e.name, e.in_use);
      return false;
    }
  }
  return true;
}

/* Set offset for directory DIR */
void
dir_set_pos(struct dir *dir, off_t pos)
{
  dir->pos = pos;
}

/* Get offset for directory DIR */
off_t
dir_get_pos(struct dir *dir )
{
  return dir->pos;
}
