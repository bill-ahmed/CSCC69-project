#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
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

struct frame_table_entry 
{
    struct list_elem elem;  // The list element
    struct thread *owner;   // Owner thread of this frame
    void *page;             // Memory address to base of page    

    struct sup_page_table_entry *spte; 
};

void ft_init ();
void *ft_allocate (enum palloc_flags flags);
void ft_free_page (void *page);

struct frame_table_entry *ft_find_page(void *page);

#endif