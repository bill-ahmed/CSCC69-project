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
  since 0 is reserved and it tells us this part hasn't been
  allocated yet. */
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

  block_sector_t sector = -1;

  /* If the offset position pos is < EOF then we assume the
     sectors are set up and indexed correctly */

  block_sector_t file_sector_index = pos / BLOCK_SECTOR_SIZE;

  /* Direct block index [0, 9] */
  if (file_sector_index < 10)
  {
    sector = inode->data.data_blocks[file_sector_index];
    return sector > 0 ? sector : -1;  
  }

  /* Must be either direct or indirect */
  else
  {
    file_sector_index -= 10;

    /* 1 level indirect [10] */
    if (file_sector_index < 128)
    {
      sector = indirect_sector_lookup(inode->data.data_blocks[10], file_sector_index);
      return sector > 0 ? sector : -1;
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
      sector = indirect_sector_lookup(indirect_idx, file_sector_index % 128);
      return sector > 0 ? sector : -1;
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
block_sector_t 
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
  int INDIRECT_INDEX = 10;
  int DBL_INDIRECT_INDEX = 11;
  block_sector_t level1_idx = NULL;
  block_sector_t level2_idx = NULL;
  block_sector_t sector_number = NULL;
  
  uint8_t zero_buffer[BLOCK_SECTOR_SIZE];
  memset(zero_buffer, 0, BLOCK_SECTOR_SIZE);
  
  /* ====== DIRECT BLOCK CHECKING ======= */

  /* Loop through each direct sector in table */
  level1_idx = find_first_zero(inode_disk->data_blocks, 10);

  /* Direct sector allocation */
  if (level1_idx != -1)
  {
    if (free_map_allocate(1, &sector_number))
    {
      /* Update the lookup table and return the sector index */
      inode_disk->data_blocks[level1_idx] = sector_number;
      block_write(fs_device, sector_number, zero_buffer);
      return sector_number;
    }
    else
      return -1;
  }

  /* ====== INDIRECT BLOCK CHECKING ======= */

  /* Must be either direct or indirect */
  else
  {
    /* Special case where indirect block needs to be set up */
    if (inode_disk->data_blocks[INDIRECT_INDEX] == 0)
    {
      if (free_map_allocate(1, &sector_number))
      {
        /* Update the lookup table and return the sector index */
        inode_disk->data_blocks[INDIRECT_INDEX] = sector_number;
        block_write(fs_device, sector_number, zero_buffer);
      }
      else
        return -1;
    }

    /* Grab the indirect block and check for first zero */
    block_sector_t *single_indirect_buffer = malloc(128 * sizeof(block_sector_t));
    if( single_indirect_buffer == NULL)
      return -1;
    
    memset(single_indirect_buffer, 0, BLOCK_SECTOR_SIZE);
    block_read(fs_device, inode_disk->data_blocks[INDIRECT_INDEX], single_indirect_buffer);
    level1_idx = find_first_zero(single_indirect_buffer, 128);

    /* If we found an non allocated direct block, create it */
    if (level1_idx != -1)
    {
      if (free_map_allocate(1, &sector_number))
      {
        /* Update the lookup table and return the sector index */
        single_indirect_buffer[level1_idx] = sector_number;
        block_write(fs_device, sector_number, zero_buffer);
        block_write(fs_device, inode_disk->data_blocks[INDIRECT_INDEX], single_indirect_buffer);
        free(single_indirect_buffer);
        return sector_number;
      }
      else
      {
        free(single_indirect_buffer);
        return -1;
      }
    }

    /* ====== DOUBLE INDIRECT BLOCK CHECKING ======= */

    /* File size > 69 KB, Must be double indirect then */
    else {
      //PANIC(">> BIG FILES NOT SUPPORTED YET");
      block_sector_t *single_indirect_buffer = malloc(128 * sizeof(block_sector_t));
      block_sector_t *double_indirect_buffer = malloc(128 * sizeof(block_sector_t));
      if (single_indirect_buffer == NULL || double_indirect_buffer == NULL)
        return -1;

      memset(single_indirect_buffer, 0, BLOCK_SECTOR_SIZE);
      memset(double_indirect_buffer, 0, BLOCK_SECTOR_SIZE);

      /* Special case where double indirect block needs to be set up */
      if (inode_disk->data_blocks[DBL_INDIRECT_INDEX] == 0)
      {
        if (free_map_allocate(1, &sector_number))
        {
          /* Update the lookup table and return the sector index */
          inode_disk->data_blocks[DBL_INDIRECT_INDEX] = sector_number;
          block_write(fs_device, sector_number, zero_buffer);
        }
        else
          return -1;
      }

      /* Grab the double indirect block and check for first zero */
      block_read(fs_device, inode_disk->data_blocks[DBL_INDIRECT_INDEX], double_indirect_buffer);

      /* Special case where first is zero, then create the indirect block */
      if (double_indirect_buffer[0] == 0)
      {
        if (free_map_allocate(1, &sector_number))
        {
          /* Update the lookup table and return the sector index */
          double_indirect_buffer[0] = sector_number;
          block_write(fs_device, sector_number, zero_buffer);
          block_write(fs_device, inode_disk->data_blocks[DBL_INDIRECT_INDEX], double_indirect_buffer);
        }
        else
          return -1;
      }

      /* Grab the indirect block of the one before the first zero */
      level1_idx = find_first_zero(double_indirect_buffer, 128);

      /* If it found an empty indirect block */
      if (level1_idx != -1)
      {
        /* We are not just able to grab the next 0 indirect in general since there
        might be some free sectors in an indirect block that has already been
        allocated. So we need to check (first_zero - 1) first before trying 
        to just allocate the next indirect sector and use that. Otherwise we
        would only ever be using the first direct block of each indirect block. */

        /* level1_idx > 0 since we have already created the 0th indirect block above 
          if it did not exist */
        
        /* Check level1_idx - 1 */
        block_read(fs_device, double_indirect_buffer[level1_idx - 1], single_indirect_buffer);
        level2_idx = find_first_zero(single_indirect_buffer, 128);
        if (level2_idx != -1)
        {
          /* Make a direct block */
          if (free_map_allocate(1, &sector_number))
          {
            /* Update the lookup table and return the sector index */
            single_indirect_buffer[level2_idx] = sector_number;
            block_write(fs_device, sector_number, zero_buffer);
            /* Write the updated buffer back to the indirect block */
            block_write(fs_device, double_indirect_buffer[level1_idx - 1], single_indirect_buffer);
            free(single_indirect_buffer);
            free(double_indirect_buffer);
            return sector_number;
          }
          else
            return -1;
        }
        
        /* If it didn't find one then the previous is full. Make a new indirect block */
        if (free_map_allocate(1, &sector_number))
        {
          /* Update the lookup table and return the sector index */
          double_indirect_buffer[level1_idx] = sector_number;
          block_write(fs_device, sector_number, zero_buffer);
          block_write(fs_device, inode_disk->data_blocks[DBL_INDIRECT_INDEX], double_indirect_buffer);
        }
        else
          return -1;

        /* We can memset the data buffer instead of reading in the zeros we
          know to already be in that new block. SPEEEED */
        memset(single_indirect_buffer, 0, BLOCK_SECTOR_SIZE);

        /* Make a direct block at the 0th index */
        if (free_map_allocate(1, &sector_number))
        {
          /* Zero the new block */
          block_write(fs_device, sector_number, zero_buffer);
          
          /* Update the indirect block table */
          (single_indirect_buffer[0]) = sector_number;
          block_write(fs_device, double_indirect_buffer[level1_idx], single_indirect_buffer);

          free(single_indirect_buffer);
          free(double_indirect_buffer);
          return sector_number;
        }
        else
          return -1;
      }

      /* Otherwise each of the indirect indexes were already filled in so
        check the last one to make sure the file is indeed full full */
      else
      {
        block_read(fs_device, double_indirect_buffer[127], single_indirect_buffer);
        level2_idx = find_first_zero(single_indirect_buffer, 128);
        
        /* If there is a zero index in the last indirect block */
        if (level2_idx != -1)
        {
          /* Make a direct block */
          if (free_map_allocate(1, &sector_number))
          {
            /* Zero the new block */
            block_write(fs_device, sector_number, zero_buffer);

            /* Update the indirect block table */
            single_indirect_buffer[level2_idx] = sector_number;
            block_write(fs_device, double_indirect_buffer[127], single_indirect_buffer);

            free(single_indirect_buffer);
            free(double_indirect_buffer);
            return sector_number;
          }
          else
            return -1;          
        }

        /* There is no zero index in the last indirect block. Completely full */
        else
        {
          return -1;
        }
      }
    }
  }
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
    size_t sectors = bytes_to_sectors (length) + 1;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->type = isDir ? INODE_TYPE_DIR : INODE_TYPE_FILE;
    disk_inode->parent = parent_sector;
    disk_inode->eof = length;
    memset(disk_inode->data_blocks, 0, 12 * sizeof(block_sector_t));

    /* For each sector we need to write to, free_map_allocate it
    then mark is as being written to in the sectors_in_use list.
    After done writing, remove it from the in use list. */
    for (size_t sectors_left_to_write = sectors; sectors_left_to_write > 0; sectors_left_to_write--)
    {
      /* Synchronization here? */
      block_sector_t new_sec = extend_one_sector(disk_inode);
      if (new_sec == -1)
      {
        printf(">> Could not increase file size\n");
        return false;
      }
      // printf(">> Extended a file by adding sector %d\n", new_sec);
    }

    /* Write the new disk inode to it's sector */
    block_write(fs_device, sector, disk_inode);
    success = true;
    // printf(">> Created Inode at sector: %d\n", sector);

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
  //printf(">> Offset: %d, EOF: %d\n", offset, inode->data.eof);
  if (offset > inode->data.eof)
  {
    off_t byte_difference = offset - inode->data.eof;
    size_t sectors_to_create = bytes_to_sectors(byte_difference);
    //printf(">> Trying to make %d new sectors to fill in space \n", sectors_to_create);
    while (sectors_to_create > 0)
    {
      if (extend_one_sector(&inode->data) == -1)
      {
        printf(">> Could not allocate enough space to grow file to write at byte %d\n", offset);
        return bytes_written;
      }
      sectors_to_create--;
    }

    inode->data.eof = offset;
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
    uint8_t *data = malloc(BLOCK_SECTOR_SIZE);
    if (!data)
      return bytes_written;

    block_sector_t sector = byte_to_sector(inode, offset);

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
      inode->data.eof = offset;
    }

    /* We will need more space for the next write */
    if (inode->data.eof == offset && byte_to_sector(inode, offset) == -1)
    {
      sector = extend_one_sector(&inode->data);
      //printf(">> Tried to extend file: %d\n", sector);
      if (sector == -1)
      {
        /* Space could not be allocated to extend */
        free(data);
        return bytes_written;
      }
    }
    free(data);
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



