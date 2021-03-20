#include "page.h"
#include "threads/vaddr.h"
#include "list.h"
#include "userprog/pagedir.h"
#include "frame.h"
#include "threads/malloc.h"
#include "userprog/process.h"

/* Find a supplementary page table entry in the current process' table */
struct sup_page_table_entry *
spt_get_entry (void *upage)
{
    /* Round down to nearest (top of page) base */
    void *base = pg_round_down (upage);

    struct sup_page_table_entry *spte;
    struct thread *t = thread_current ();
    struct list *spt_list = &t->sup_page_table;
    struct list_elem *e;

    for (e = list_begin(spt_list); e != list_end(spt_list); e = list_next(e))
    {
        spte = list_entry (e, struct sup_page_table_entry, elem);
        if (spte->upage == base)
        {
            return spte;
        }
    }

    /* Counldn't find it */
    return NULL;
}

/* Removes an entry from a process' supplementary page table */
void spt_remove_entry (struct sup_page_table_entry *spte)
{
    /* Remove from the local process' spt */
    list_remove (&spte->elem);

    /* Get physical addr */
    struct thread *t = thread_current ();
    uint8_t *kpage = pagedir_get_page (t->pagedir, spte->upage);

    /* Set the bitmap of the page to unused */
    pagedir_clear_page (t->pagedir, spte->upage);
    
    /* Free the page from the frame table */
    ft_free_page (kpage);

    free (spte);
}

bool 
spt_load_from_file (struct sup_page_table_entry *spte)
{
    bool success = false;

    /* Set the flags for user page */
    enum palloc_flags flags = PAL_USER;

}

bool 
spt_grow_stack_by_one (void *vaddr)
{
    /* We will have already checked whether this vaddr is for the stack */

    struct sup_page_table_entry *spte = malloc (sizeof(struct sup_page_table_entry));
    if (spte == NULL) 
    {
        /* Kernel pool full */
        return false;
    }

    void *upage_base = pg_round_down (vaddr);

    /* Fill out the page table entry */
    spte->owner = thread_current ();
    spte->upage = upage_base;
    spte->type = PAGE_STACK;
    spte->in_swap = false;
    spte->writable = true;

    /* Get a frame base and install the page there */
    uint8_t *frame_base = ft_allocate (PAL_USER);
    bool success = install_page (upage_base, frame_base, spte->writable);

    if (!success)
    {
        /* Free the resources we allocated */
        free (spte);
        free (frame_base); 
        return false;
    }

    /* Add the spte to the thread's list */
    list_push_back(&spte->owner->sup_page_table, &spte->elem);
    return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
    struct thread *t = thread_current();

    /* Verify that there's not already a page at that virtual
     address, then map our page there. */
    return (pagedir_get_page(t->pagedir, upage) == NULL && 
        pagedir_set_page(t->pagedir, upage, kpage, writable));
}