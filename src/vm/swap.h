#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "lib/kernel/list.h"
#include "devices/block.h"
#include "threads/vaddr.h"

struct block* global_swap;

// Prevent reading/writing at the same time!
struct lock swap_modify_lock;

// Maximum number of entries that can be swapped
#define MAX_SWAP_ENTRIES 1024

// Number of blocks to write in order to fit a single page.
#define BLOCKS_IN_SWAP (PGSIZE / BLOCK_SECTOR_SIZE)

void swap_init();

int swap_allocate(void *frame);
void swap_read(void *frame, int index);
void swap_free(int index);
void swap_print_status();


#endif