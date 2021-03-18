#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/list.h"
#include "threads/thread.h"

/*
    A supplemental page entry is owned by a process.
    This keeps track of information about a user page.
*/

/* We need to know what kind of page we are dealing with */
#define PAGE_HEAP 1
#define PAGE_STACK 2
#define PAGE_CODE 3
#define PAGE_MMAP 4

struct sup_page_table_entry 
{
    struct list_elem elem;      /* The list element */
    void *upage;                /* Pointer to base of user page */
    
    bool in_swap;               /* Whether has been swapped out */
    bool marked_for_swap;       /* Whether has been marked to swap out by LRU */
    uint8_t type;               /* One of PAGE_STACK, PAGE_CODE, etc */
};

/* Functions */
struct sup_page_table_entry *spt_get_entry (void *upage);
void spt_remove_entry (struct sup_page_table_entry *spte);
bool spt_load_from_file(struct sup_page_table_entry *spte);

#endif