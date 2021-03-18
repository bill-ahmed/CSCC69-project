#include "frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/palloc.h"

void 
ft_init ()
{
    list_init (&frame_table_list);
}

void *
ft_allocate(enum palloc_flags flags)
{
    // Get a new page
    void *page = palloc_get_page (flags);

    if (page != NULL)
    {
        // Allocate a new frame table entry
        struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
        fte->owner = thread_current ();
        fte->page = page;   // Make sure it points to the base of the page

        // Add the frame to the list and release lock
        list_push_back (&frame_table_list, &fte->elem);
    }

    // Page not returned, we must swap and try again. 
    else
    {
        /* Page eviction:
            1. Select candidate page in frame table based
            on LRU algorithm.

            2. Write contents of candidate page into "swap" file
                2.1. Make sure to call pagedir_clear_page to free
                that memeory from the user pool.

                2.2. Remove that 

            3. Allocate a new page (like shown above) and push
            into frame list
        */
    }

    return page;
}

void 
ft_free_page (void *page)
{
    /* Find pte in frame_table_list */
    struct frame_table_entry *fte = ft_find_page (page);

    if (fte == NULL)
    {
        /* Can't free if we can't find it */
        return;
    }

    list_remove (&fte->elem);
    palloc_free_page (fte->page);
    free (fte);
}

/* Find the fte in the list pointing to this page */
struct frame_table_entry *
ft_find_page (void *page)
{
    struct frame_table_entry *fte;

    struct list_elem *elem;
    for (elem = list_begin (&frame_table_list); 
        elem != list_end (&frame_table_list); 
        elem = list_next (elem))
    {
        fte = list_entry (elem, struct frame_table_entry, elem);
        
        /* Check virutal base address matches, and owner */
        if (fte->page == page && fte->owner == thread_current ())
        {
            return fte;
        }
    }

    /* Didn't find it */
    return NULL;
}