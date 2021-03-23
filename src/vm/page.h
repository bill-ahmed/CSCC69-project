#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/list.h"
#include "threads/thread.h"

/*
    A supplemental page entry is owned by a process.
    This keeps track of information about a page.
*/

/* What kind of page is allocated */
enum page_type
{
    PAGE_HEAP,
    PAGE_STACK,
    PAGE_CODE,
    PAGE_MMAP
};

struct sup_page_table_entry 
{
    struct list_elem elem;      /* The list element */
    void *upage;                /* Pointer to base of user page */
    
    bool in_swap;               /* Whether has been swapped out */
    bool marked_for_swap;       /* Whether has been marked to swap out by LRU */
    bool writable;              /* Whether able to be written (modified) to */
    enum page_type type;        /* One of PAGE_STACK, PAGE_CODE, etc */

    /* Load on demand data */
    struct file *file;      /* The file to grab data from */
    off_t file_offset;      /* Where this page starts in the file */
    size_t page_read_bytes; /* Number of bytes of data to read */
    size_t page_zero_bytes; /* Number of bytes to fill with zeros */

    int swap_index;             /* Which block sector this page is in the swap disk. */
    struct thread *owner;       /* The owner of the spte */
    
};

/* Functions */
struct sup_page_table_entry *spt_get_entry (void *upage);
void spt_remove_entry (struct sup_page_table_entry *spte);
bool spt_load_from_file(struct sup_page_table_entry *spte);
bool swap_into_memory(struct sup_page_table_entry *spte);
bool spt_grow_stack_by_one(void *vaddr);

bool install_page(void *upage, void *kpage, bool writable);
#endif