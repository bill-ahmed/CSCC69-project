#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  /* The inode stored on disk is now sector indexes to
      a direct or indirect data block */
  block_sector_t data_blocks[12];       /*  */
  unsigned magic;                       /* Magic number. */
  off_t eof;                            /* Byte where the EOF is located. Filesize in bytes */
  uint32_t unused[114];                 /* Not used. */
  
  /* (12 + 1 + 1 + 114) * 4 bytes each = 512 */
};

/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* PROTOTYPES */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos);
static inline size_t bytes_to_sectors(off_t size);

/* INODE FUNCTIONS */

/* Initializes the inode module. */
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
inode_create (block_sector_t sector, off_t length, bool isDir, block_sector_t parent_sector)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->type = isDir ? INODE_TYPE_DIR : INODE_TYPE_FILE;
      disk_inode->parent = parent_sector;
      
      /* For each sector we need to write to, free_map_allocate it
        then mark is as being written to in the sectors_in_use list.
        After done writing, remove it from the in use list. */
      for (size_t sectors_left_to_write = sectors; sectors_left_to_write > 0; sectors_left_to_write--)
      {

      }

      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          int bytes = length % BLOCK_SECTOR_SIZE;

          printf(">> Created Inode at sector: %d, with length: %d sectors, and %d remaining bytes \n", disk_inode->start, sectors, bytes);
          if (sectors > 0)
          {
            static char zeros[BLOCK_SECTOR_SIZE];
            size_t i;

            for (i = 0; i < sectors; i++)
              block_write(fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
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

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  // inode->eof = inode->data.length ?
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
/* TODO */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
/* TODO */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
/* TODO */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
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
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.eof;
}

/* HELPERS */

/* Returns the sector index stored in an indirect block, -1 if invalid */
block_sector_t 
indirect_sector_lookup(block_sector_t sector, uint32_t offset)
{
  ASSERT(offset < 128 && offset >= 0);

  /* 128 * 4 byte numbers = 512 bytes */
  block_sector_t *data = malloc(BLOCK_SECTOR_SIZE);
  if (!data)
  {
    PANIC(">> malloc failed to allocate 512 bytes");
    return -1;
  }

  block_read(fs_device, sector, data);
  block_sector_t found_index = data[offset];
  free(data);

  /* We can use 0 to determine whether it is a valid sector 
  since only ever the root dir is at 0. */
  return (found_index == 0) ? -1 : found_index;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. 
   There is no need to synchronize this since all the indirect 
   block data associated with a file at any pos <= EOF is static. */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
  /*
    data_blocks indexes:
    - 0-9 are direct blocks
    - 10 is an indirect block
    - 11 is a double indirect block
  */

  ASSERT(inode != NULL);
  if (pos > inode->data.eof)
  {
    /* There shouldn't be any data beyond the EOF */
    return -1;
  }

  /* If the offset position pos is < EOF then we assume the
     sectors are set up and indexed correctly */

  block_sector_t file_sector_index = pos / BLOCK_SECTOR_SIZE;

  /* Direct block index [0, 9] */
  if (file_sector_index < 10)
  {
    return inode->data.data_blocks[file_sector_index];
  }

  /* Must be either direct or indirect */
  else
  {
    file_sector_index -= 10;
    
    /* 1 level indirect [10] */
    if (file_sector_index < 128)
    {
      return indirect_sector_lookup(inode->data.data_blocks[10], file_sector_index);
    }

    /* 2 level indirect [11] */
    else
    {
      file_sector_index -= 128;
      /* file_sector_index is now between 0 and 128*128=16384 */

      /* From the double indirect block, we get the indirect sector index */
      block_sector_t dbl_indirect_index = file_sector_index / 128;
      block_sector_t indirect_idx = indirect_sector_lookup(inode->data.data_blocks[11], dbl_indirect_index);
      if (indirect_idx == -1) {
        return -1;
      }

      /* Using the indirect sector index, we get one of the 128 data blocks */
      return indirect_sector_lookup(indirect_idx, file_sector_index % 128);
    }
  }
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors(off_t size)
{
  return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* Allocates a sector in the file system and adds it to the
   file's lookup blocks as necessary. Does not move file EOF */
/* TODO */
bool 
extend_one_sector(struct inode *inode)
{
  
  return false;
}
