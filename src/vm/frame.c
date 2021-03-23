#include "frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/interrupt.h"
#include "userprog/pagedir.h"
#include "list.h"
#include "vm/swap.h"

struct frame_table_entry * ft_find_evict_page();

void 
ft_init ()
{
    list_init (&frame_table_list);
    lock_init (&frame_table_lock);
}

void *
ft_allocate (enum palloc_flags flags)
{
  struct thread *curr = thread_current ();

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
      lock_acquire (&frame_table_lock);

    // Add the frame to the list and release lock
    list_push_back (&frame_table_list, &fte->elem);
    lock_release (&frame_table_lock);
  }
  // Page not returned, we must swap and try again. 
  else
  {
    // Step 1 - Select candidate page in frame table based on LRU algorithm.
    struct frame_table_entry *fte = ft_find_evict_page ();
    struct sup_page_table_entry *sup = fte->spte;

    // Step 2 - Write contents of candidate page into "swap" file
    sup->swap_index = swap_allocate (fte->page);
    sup->in_swap = true;

    // Step 2.1 - Make sure to call pagedir_clear_page to free that memory from the user pool.
    pagedir_clear_page(fte->owner->pagedir, fte->spte->upage);
    ft_free_fte (fte);

    // Step 3: Allocate a new page (like shown above) and push into frame list
    page = palloc_get_page (flags);

    if (page != NULL)
    {
      // Allocate a new frame table entry
      struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));

      fte->owner = curr;
      fte->page = page;   // Make sure it points to the base of the page
      fte->pinned = false;

      if (!lock_held_by_current_thread (&frame_table_lock))
        lock_acquire (&frame_table_lock);

      // Add the frame to the list and release lock
      list_push_back (&frame_table_list, &fte->elem);
      lock_release (&frame_table_lock);
    }
    else
    {
      PANIC("Failed to free a page frame.\n");
    }
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

    /* Immediately mark as pinned so it doesn't get swapped */
    fte->pinned = true;

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

void
ft_free_fte(struct frame_table_entry *fte)
{
  fte->pinned = true;
  
  if (!lock_held_by_current_thread(&frame_table_lock))
  {
      lock_acquire(&frame_table_lock);
  }

  list_remove(&fte->elem);
  palloc_free_page(fte->page);
  free(fte);

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

/* Clear all frame table entries for current thread. */
void
ft_clear_thread_pages()
{
  struct thread *curr = thread_current ();

  struct list_elem *elem;
  struct frame_table_entry *fte;

  for(
    elem =  list_begin (&frame_table_list); 
    elem != list_end (&frame_table_list); 
    elem =  list_next (elem)
  )
  {
    fte = list_entry (elem, struct frame_table_entry, elem);
    
    if(fte->owner == curr)
    {
      list_remove(&fte->elem);
      palloc_free_page(fte->page);

      spt_remove_entry(fte->spte);
    }
  }
}

/* Finds a page to evict. */
struct frame_table_entry *
ft_find_evict_page()
{
  struct list_elem *elem;
  struct frame_table_entry *fte;

  if (!lock_held_by_current_thread (&frame_table_lock))
    lock_acquire (&frame_table_lock);

  for(
    elem =  list_begin (&frame_table_list); 
    elem != list_end (&frame_table_list); 
    elem =  list_next (elem)
  )
  {
    fte = list_entry (elem, struct frame_table_entry, elem);
    if(!fte->spte)
      continue;
    
    if(fte->spte->writable && !fte->pinned)
    {
      fte->pinned = true;
      lock_release(&frame_table_lock);
      return fte;
    }
  }

  // Take first writable frame
  for(
    elem =  list_begin (&frame_table_list); 
    elem != list_end (&frame_table_list); 
    elem =  list_next (elem)
  )
  {
    fte = list_entry (elem, struct frame_table_entry, elem);
    if(!fte->pinned)
    {
      fte->pinned = true;
      lock_release(&frame_table_lock);
      return fte;
    }
  }

  // No page to evict found, remove first one
  fte = list_entry (list_begin (&frame_table_list), struct frame_table_entry, elem);
  fte->pinned = true;
  lock_release(&frame_table_lock);
  return fte;
}
