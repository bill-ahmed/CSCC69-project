#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "lib/stdlib.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

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
      if (indirect_idx == -1)
      {
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

/* Finds the index of first occurrance of a zero in buffer.
   -1 otherwise. */
int 
find_first_zero(block_sector_t *buffer, unsigned int length)
{
  block_sector_t *ptr = buffer;
  for (int index = 0; index < length; index++, ptr++)
  {
    if (*ptr == 0)
    {
      return index;
    }
  }
  return -1;
}

/* Allocates a sector in the file system, zeros, and adds it to the
   file's lookup blocks as necessary. Does not move file EOF */
/* TODO */
block_sector_t 
extend_one_sector(struct inode_disk *inode_disk)
{
  ASSERT(inode_disk != NULL);
  /*
    data_blocks indexes:
    - 0-9 are direct blocks
    - 10 is an indirect block
    - 11 is a double indirect block
  */
  uint8_t zero_buffer[BLOCK_SECTOR_SIZE];
  memset(zero_buffer, 0, BLOCK_SECTOR_SIZE);
  block_sector_t sector_number = -1;

  /* Loop through each direct sector in table */
  off_t seq_idx = find_first_zero(inode_disk->data_blocks, 10);

  /* Direct sector allocation */
  if (seq_idx != -1)
  {
    if (free_map_allocate(1, &sector_number))
    {
      /* Update the lookup table and return the sector index */
      inode_disk->data_blocks[seq_idx] = sector_number;
      block_write(fs_device, sector_number, zero_buffer);
      return sector_number;
    }
    else
      PANIC(">> Ran out of filespace");
  }

  /* Must be either direct or indirect */
  else
  {
    /* Special case where indirect block needs to be set up */
    if (inode_disk->data_blocks[10] == 0)
    {
      /* Make the indirect block */
      if (free_map_allocate(1, &sector_number))
      {
        /* Write the sector of the indirect block */
        inode_disk->data_blocks[10] = sector_number;
        block_write(fs_device, sector_number, zero_buffer);
      }
      else
        PANIC(">> Ran out of filespace");
    }

    /* Grab the indirect block and check for first zero */
    block_sector_t data_buffer[128];
    block_read(fs_device, inode_disk->data_blocks[10], data_buffer);
    seq_idx = find_first_zero(data_buffer, 128);

    /* If we found an non allocated direct block, create it */
    if (seq_idx != -1)
    {
      /* Make the direct block */
      if (free_map_allocate(1, &sector_number))
      {
        /* Zero the new block */
        block_write(fs_device, sector_number, zero_buffer);

        /* Update the indirect block */
        data_buffer[seq_idx] = sector_number;
        block_write(fs_device, inode_disk->data_blocks[10], data_buffer);

        /* Return number of direct block */
        return sector_number;
      }
      else
        PANIC(">> Ran out of filespace");
    }

    /* Must be double indirect then */
    PANIC(">> Tried to allocate a sector of a file > 69 KB. Not implemented yet\n");
  }
  return -1;
}

/* INODE FUNCTIONS */

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  list_init (&sectors_in_use);
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

  disk_inode = malloc(sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->magic = INODE_MAGIC;
    disk_inode->type = isDir ? INODE_TYPE_DIR : INODE_TYPE_FILE;
    disk_inode->parent = parent_sector;
    disk_inode->eof = length;
    memset(disk_inode->data_blocks, 0, 12 * sizeof(block_sector_t));

    /* Allocated space for the inode itself */
    block_sector_t inode_sector;
    if (free_map_allocate(1, &inode_sector)) 
    {
      // printf(">> Created Inode at sector: %d\n", sector);

      /* For each sector we need to write to, free_map_allocate it
      then mark is as being written to in the sectors_in_use list.
      After done writing, remove it from the in use list. */
      for (size_t sectors_left_to_write = sectors; sectors_left_to_write > 0; sectors_left_to_write--)
      {
        /* Synchronization here? */

        if (extend_one_sector(disk_inode) == -1)
        {
          printf(">> Could not increase file size\n");
          return false;
        }
      }

      /* Write the new disk inode to it's sector */
      block_write(fs_device, sector, disk_inode);
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

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) 
    {
      unsigned int sectors_to_release = bytes_to_sectors(inode->data.eof);
      free_map_release(inode->sector, 1);
      
      /* In 512 byte offset increments, find the corresponding sector, and free it */
      for (int sec = 0; sec < sectors_to_release; sec++)
      {
        free_map_release (byte_to_sector(inode, sec * BLOCK_SECTOR_SIZE), 1); 
      }
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
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  
  /* While there are still bytes left to read */
  while (size > 0)
  {
    /* Can't read any data outside the bounds of the file */
    if (offset >= inode->data.eof)
    {
      break;
    }

    /* Calculate where to start, and how many bytes to read in this sector */
    off_t sector_ofs = offset % BLOCK_SECTOR_SIZE;
    off_t sector_bytes_to_read = BLOCK_SECTOR_SIZE - sector_ofs;

    if (size < sector_bytes_to_read)
    {
      sector_bytes_to_read = size;
    }

    /* Grab the corresponding sector (TODO: Check cache) */
    uint8_t data[BLOCK_SECTOR_SIZE];
    block_sector_t sector = byte_to_sector(inode, offset);
    if (sector == -1)
    {
      break;
    }

    /* Read the sector data in local variable */
    block_read(fs_device, sector, data);
    memcpy(buffer + bytes_read, data + sector_ofs, sector_bytes_to_read);

    /* Advance. */
    size -= sector_bytes_to_read;
    offset += sector_bytes_to_read;
    bytes_read += sector_bytes_to_read;
  }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if file can not be extended further or an 
   error occurs. 
   
   Writing beyond EOF will cause the file to try to fill in the
   missing gap with 0 bytes, then continue writing and file 
   extension as normal. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* If the write offset is beyond the EOF, allocate missing sectors 
     in between and fill in with 0 bytes */
  if (offset > inode->data.eof)
  {
    /* Determine whether the offset is within the same sector as EOF */
    off_t sector_ofs = offset % BLOCK_SECTOR_SIZE;
    off_t eof_ofs = inode->data.eof % BLOCK_SECTOR_SIZE;
    off_t byte_difference = offset - inode->data.eof;

    if (sector_ofs + (byte_difference) > BLOCK_SECTOR_SIZE)
    {
      /* Write in the bytes between EOF and boundary to be zeros */
      uint8_t data[BLOCK_SECTOR_SIZE];
      block_sector_t EOF_sector = byte_to_sector(inode, inode->data.eof);
      block_read(fs_device, EOF_sector, data);
      memset(data + eof_ofs, 0, BLOCK_SECTOR_SIZE - eof_ofs);
      block_write(fs_device, EOF_sector, data);

      /* The offset to start writing to lies beyond the sector boundary
      where the EOF is located */
      size_t sectors_to_create = bytes_to_sectors(byte_difference);
      while (sectors_to_create > 0)
      {
        if (extend_one_sector(&inode->data) == -1)
        {
          printf(">> Could not allocate enough space to grow file to write at byte %d\n", offset);
          return 0;
        }
        sectors_to_create--;
      }

      /* Extend the EOF temporarily to where the write intends to begin */
      inode->data.eof = offset;
    }
  }

  /* Now offset <= EOF */
  /* While there are still bytes left to write */
  while (size > 0)
  {
    /* Calculate where to start, and how many bytes to read in this sector */
    off_t sector_ofs = offset % BLOCK_SECTOR_SIZE;
    off_t sector_bytes_to_write = BLOCK_SECTOR_SIZE - sector_ofs;

    if (size < sector_bytes_to_write)
    {
      sector_bytes_to_write = size;
    }

    /* Grab the corresponding sector (TODO: Check cache) */
    uint8_t data[BLOCK_SECTOR_SIZE];
    block_sector_t sector = byte_to_sector(inode, offset);
    if (sector == -1)
    {
      /* File does not contain a sector with that byte offset. Extend the file */
      sector = extend_one_sector(&inode->data);
      // printf(">> Tried to extend file: %d\n", sector);
      if (sector == -1)
      {
        /* Space could not be allocated to extend */
        return bytes_written;
      }
    }

    /* Read the sector data in local variable */
    block_read(fs_device, sector, data);

    /* TODO: Write to cache */
    memcpy(data + sector_ofs, buffer + bytes_written, sector_bytes_to_write);
    block_write(fs_device, sector, data);

    /* Advance. */
    size -= sector_bytes_to_write;
    offset += sector_bytes_to_write;
    bytes_written += sector_bytes_to_write;
    if (offset > inode->data.eof)
    {
      /* Extend the EOF to however many bytes we wrote past it */
      inode->data.eof += (offset - inode->data.eof);
    }
  }

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



