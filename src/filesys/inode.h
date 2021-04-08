#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>

struct bitmap;

/* List of sectors in the fs being written to atm */
struct list sectors_in_use;

/* Used to keep track of which sectors are being written
to in the file system block */
struct sector_lock
{
    block_sector_t sector;
    struct list_elem elem;
};

/* What the inode represents */
enum inode_type {
  INODE_TYPE_FILE,
  INODE_TYPE_DIR,
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
    /* The inode stored on disk is now sector indexes to
      a direct or indirect data block */
    block_sector_t data_blocks[12]; /*  */
    unsigned magic;                 /* Magic number. */
    off_t eof;                      /* Byte where the EOF is located. Filesize in bytes */
    block_sector_t parent;          /* Sector number of parent, if type == INODE_TYPE_DIR, NULL otherwise. */
    enum inode_type type;           /* The type of inode */
    uint32_t unused[112];           /* Not used. */

    /* 4*12 + 4*1 + 4*1 + 4*1 + 4*1 + 4*112 = 512 */
};

/* In-memory inode. */
struct inode 
{
  struct list_elem elem;              /* Element in inode list. */
  block_sector_t sector;              /* Sector number of disk location. */
  int open_cnt;                       /* Number of openers. */
  bool removed;                       /* True if deleted, false otherwise. */
  int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  struct inode_disk data;             /* Inode content. */
};

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool, block_sector_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
