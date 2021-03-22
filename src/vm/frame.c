#include "frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/palloc.h"

void 
ft_init ()
{
    list_init (&frame_table_list);
    lock_init (&frame_table_lock);
}

void *
ft_allocate (enum palloc_flags flags)
{
    // Get a new page
    void *page = palloc_get_page (flags);

    if (page != NULL)
    {
        // Allocate a new frame table entry
        struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
        fte->owner = thread_current ();
        fte->page = page;   // Make sure it points to the base of the page
        fte->pinned = false;

        if (!lock_held_by_current_thread (&frame_table_lock))
        {
            lock_acquire (&frame_table_lock);
        }

        // Add the frame to the list and release lock
        list_push_back (&frame_table_list, &fte->elem);
        lock_release (&frame_table_lock);
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

    /* Immediately mark as pinned so it doesn't get swapped */
    fte->pinned = true;

    if (fte == NULL)
    {
        /* Can't free if we can't find it */
        return;
    }

    /* We need to aquire the lock all the way up here to
    make sure that in between finding the fte and trying
    to free it, that it didn't get swapped out by another
    thread. */
    if (!lock_held_by_current_thread(&frame_table_lock))
    {
        lock_acquire(&frame_table_lock);
    }

    list_remove (&fte->elem);
    palloc_free_page (fte->page);
    free (fte);
    lock_release (&frame_table_lock);
}

/* Find the fte in the list pointing to this page. */
struct frame_table_entry *
ft_find_page (void *page)
{
    struct frame_table_entry *fte;
    struct list_elem *elem;
    bool found = false;

    if (!lock_held_by_current_thread(&frame_table_lock))
    {
        lock_acquire(&frame_table_lock);
    }

    for (elem = list_begin (&frame_table_list); 
        elem != list_end (&frame_table_list); 
        elem = list_next (elem))
    {
        fte = list_entry (elem, struct frame_table_entry, elem);
        
        /* Check physical base address matches, and owner */
        if (fte->page == page && fte->owner == thread_current ())
        {
            found = true;
            break;
        }
    }

    lock_release(&frame_table_lock);
    return found ? fte : NULL;
}