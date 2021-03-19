#include "page.h"
#include "threads/vaddr.h"
#include "list.h"
#include "userprog/pagedir.h"
#include "frame.h"

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
void spt_remove_entry(struct sup_page_table_entry *spte)
{
    /* Remove from the local process' spt */
    list_remove (&spte->elem);

    /* Get physical addr */
    struct thread *t = thread_current ();
    uint8_t *kpage = pagedir_get_page(t->pagedir, spte->upage);

    /* Set the bitmap of the page to unused */
    pagedir_clear_page (t->pagedir, spte->upage);
    
    /* Free the page from the frame table */
    ft_free_page (kpage);

    free (spte);
}

bool 
spt_load_from_file(struct sup_page_table_entry *spte)
{
    bool success = false;

    /* Set the flags for user page */
    enum palloc_flags flags = PAL_USER;

}