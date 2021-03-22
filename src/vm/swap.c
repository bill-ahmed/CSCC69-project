#include "swap.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "devices/block.h"

static int swap_list[MAX_SWAP_ENTRIES];

void
swap_init()
{
  // Where we store swapped pages
  global_swap = block_get_role(BLOCK_SWAP);
  lock_init (&swap_modify_lock);
}

/* Write page at frame to swap and return the index where it is located */
int
swap_allocate(void *frame)
{
  int free_spot;
  int next_page_pos;
  int next_block_pos;
  
  if(!lock_held_by_current_thread(&swap_modify_lock))
    lock_acquire (&swap_modify_lock);

  free_spot = find_free_index();
  if(free_spot == NULL)
    PANIC('Swap list is full!\n');
  
  swap_list[free_spot] = 1;

  // Since block size is 512 bytes and page is 4KB, 
  // we need to iterate multiple times in order to write
  // all the data to disk.
  for(int i = 0; i < BLOCKS_IN_SWAP; i++)
  {
    // Where the next page data & block sector is
    next_page_pos = (uint8_t *) frame + (i * BLOCK_SECTOR_SIZE);
    next_block_pos = free_spot*BLOCKS_IN_SWAP + i;

    // Write page data to a given block
    block_write(global_swap, next_block_pos, next_page_pos);
  }

  lock_release (&swap_modify_lock);

  return free_spot;
}

/* Read swap data into page "frame" at given index. 
   Index should have been obtained by call to swap_allocate(). */
void
swap_read(void *frame, int index)
{
  if(!lock_held_by_current_thread(&swap_modify_lock))
    lock_acquire (&swap_modify_lock);

  // If no data has been written to the swap at given index
  if(!swap_list[index])
    return NULL;

  for(int i = 0; i < BLOCKS_IN_SWAP; i++)
  {
    block_read(global_swap, index*BLOCKS_IN_SWAP + i, (uint8_t *) frame + (i * BLOCK_SECTOR_SIZE));
  }

  lock_release (&swap_modify_lock);
}

// Free up a swap allocation.
void
swap_free(int index)
{
  // Allow future writes
  swap_list[index] = NULL;
}

/* Locate a free spot in the swap table. If not free spots
   are available, it returns NULL. */
int
find_free_index()
{
  for(int i = 1; i < MAX_SWAP_ENTRIES; i++)
  {
    if(!swap_list[i])
      return i;
  }

  return NULL;
}

/* TODO: Nuke this */
void
swap_print_status()
{
  size_t open = 0;
  size_t used = 0;
  int i;

  for(i = 1; i < MAX_SWAP_ENTRIES; i++)
  {
    // printf("%d: %d\n", i, swap_list[i]);
    if(swap_list[i] == 1)
      used += 1;
    else
      open += 1;
  }

  printf(">> [Swap] Summary - Free: %d, Used: %d\n", open, used);
}
