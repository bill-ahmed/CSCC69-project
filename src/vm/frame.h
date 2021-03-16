#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"

/* 
    - A frame is a 4KB piece of contiguous memory.
    - Each frame may store the data of a virtual 4KB page.
    - This is Kernel level access only.
    - A each frame_table_entry maps to one page.
    - Each thread has it's own sup_page_table to keep track
      of its used pages.
*/

// A list of frame_table_entry as the page table
struct list frame_table_list;
struct lock frame_table_lock;

unsigned int VM_FRAME_TABLE_SIZE = 0;

struct frame_table_entry 
{
    struct list_elem elem;  // The list element
    struct thread *owner;   // Owner thread of this frame
    void *page;             // Memory address to base of page
    
    // Maybe store information for memory mapped files here too?
};

void frame_table_init ();
void *frame_table_get_page (enum palloc_flags flags);
void frame_table_free_page (void *page);

#endif