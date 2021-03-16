#include "frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"

void frame_table_init ()
{
    list_init (&frame_table_list);
    lock_init (&frame_table_lock);
}

void *frame_table_new_page (enum palloc_flags flags)
{
    // Get a new page
    void *page = palloc_get_page (flags);

    if (page != NULL)
    {
        // Allocate a new frame table entry
        struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
        fte->owner = thread_current ();
        fte->page = page;   // Make sure it points to the base of the page

        // We only want async manipulation of frames list
        if (!lock_held_by_current_thread(&frame_table_lock))
        {
            lock_acquire(&frame_table_lock);
        }

        // Add the frame to the list and release lock
        list_push_back(&frame_table_list, &fte->elem);
        lock_release(&frame_table_lock);
    }

    // Page not returned, we must swap and try again. 
    else
    {
        // TODO: Evict a page (move into swap file on disk)
    }

    return page;
}

void frame_table_free_page (void *page)
{
    // Find pte in frame_table_list with pte->page = page 
    // list_remove (pte->elem)
    // palloc_free_page (pte->page)
    // free (pte)
}