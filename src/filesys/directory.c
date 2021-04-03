#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t parent_sector)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true, parent_sector);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
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
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

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

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

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
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  off_t t = inode_write_at (dir->inode, &e, sizeof e, ofs);
  success = t == sizeof e;
 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  // Make sure directory is empty!!
  if (!dir_is_empty (dir_open (inode)))
    return false;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
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

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

/* Get parent of DIR. */
struct dir *
dir_get_parent (struct dir *dir)
{
  // printf("Opening parent at sector %d\n", dir->inode->data.type == INODE_TYPE_DIR);
  // Root is its own parent!
  block_sector_t parent = dir->inode->data.parent;
  if(parent == NULL)
    parent = ROOT_DIR_SECTOR;

  return dir_open (inode_open (parent));
}

/* Returns true iff DIR has no contents (no files and 
   no sub-directories). Returns false otherwise. */
bool
dir_is_empty (struct dir *dir)
{
  struct dir_entry e;
  size_t ofs;

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use)
      return false;
  
  return true;
}

/* Standard C library, compare N characters of two strings. 
   TODO: Refactor this... */
int
strncmp(const char *s1, const char *s2, register size_t n)
{
  register unsigned char u1, u2;
  while (n-- > 0)
    {
      u1 = (unsigned char) *s1++;
      u2 = (unsigned char) *s2++;
      if (u1 != u2)
        return u1 - u2;
      if (u1 == '\0')
        return 0;
    }
  return 0;
}

/* Resolve PATH beginning at directory START.
   Returns last accessible directory.
   Stores last segment accessed in LAST_SEGMENT, if provided. 
   
   If GIVE_LAST is true, will return the last directory encountered, else the second-last one. 
   This is useful for mkdir and chdir especially. */
struct dir *
resolve_path (char *path, struct dir *start, char last_segment[NAME_MAX + 1], bool give_last)
{
  char *path_cpy;
  char *token, *save_ptr;

  char last[NAME_MAX + 1];
  
  // Make a copy of path so we don't modify it in strtok_r
  path_cpy = malloc(sizeof(char) * strlen(path) + 1);
  strlcpy (path_cpy, path, strlen(path) + 1);

  // Reject empty path
  if(!strcmp (path, ""))
    return NULL;

  // Special case when path starts with root "/"
  if(!strncmp (path, "/", 1))
    start = dir_open_root ();

  struct dir *cwd = start;
  struct dir *prev_cwd = start;

  token = strtok_r (path_cpy, "/", &save_ptr);
  while (token != NULL)
  {
    // Token can be one of: '.', '..', or a directory name
    if(strlen(token) > NAME_MAX)
      return NULL;
    
    // Current directory
    if (!strcmp (token, "."))
    {
      // Continue with while loop
    }
    else if(!strcmp (token, ".."))
    {
      // Previous directory
      prev_cwd = cwd;
      cwd = dir_get_parent (cwd);
    }
    else
    {
      /* This token is a possible directory/file name. */
      strlcpy (last, token, strlen(token) + 1);

      if (cwd != NULL)
      {
        struct inode *inode = NULL;
        if(dir_lookup (cwd, token, &inode))
        {
          prev_cwd = cwd;
          if (is_dir (inode))
            cwd = dir_open (inode);
        }
        else
        {
          // We've hit the end, stop updating cwd and prev_cwd
          // We still continue parsing so we can make note of 
          // the last segment that appears. This would indicate 
          // the file name, directory to create, etc.
          prev_cwd = cwd;
          cwd = NULL;
        }
      }
    }

    token = strtok_r (NULL, "/", &save_ptr);
  }

  if(last_segment)
    strlcpy (last_segment, last, NAME_MAX + 1);

  // printf("[resolve] give_last: %d, cwd: %p\n", give_last, cwd);
  if (give_last)
    return cwd;

  return prev_cwd;
}

/* True iff INODE represents a directory, false otherwise*/
bool
is_dir (struct inode *inode)
{
  if(inode == NULL)
    return false;

  return inode->data.type == INODE_TYPE_DIR;
}

/* Recursively print all directories/files in filesystem */
void
print_fs(struct dir *dir, int indent)
{
  printf("\n** Printing filesystem %d **\n\n", dir->inode->sector);
  print_fs_helper (dir, indent);
  printf("\n** Done printing filesystem **\n\n");
}

void
print_fs_helper (struct dir *dir, int indent)
{
  char name[NAME_MAX + 1];
  struct inode *i;
  bool success;

  while (dir_readdir (dir, name))
  {
    int temp = indent;

    dir_lookup (dir, name, &i);

    while (temp-- > 0)
      printf("-");
      
    printf("| %s%s (%d)\n", name, !is_dir (i) ? ".txt" : "", i->sector);

    if (is_dir (i))
      print_fs_helper (dir_open (i), indent + 1);
  }
}
