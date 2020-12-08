#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

struct coremap *cm;
p_page_t first_page; // first physical page that can be allocated
p_page_t last_page; // last free physical page in RAM exclusive

// checks if a physical page has the used bit high
static bool p_page_used(p_page_t p_page)
{
    cm_entry_t entry = cm->cm_entries[p_page];
    bool used = entry & PP_USED;
    return used;
}

void vm_bootstrap(void)
{
    // initialize physical ram memory
    paddr_t cm_paddr = ram_stealmem(COREMAP_PAGES);
    KASSERT(cm_paddr != 0);

    cm = (struct coremap *) PADDR_TO_KVADDR(cm_paddr);
    spinlock_init(&cm->cm_spinlock);
    cm->cm_counter = 0;

    // check that memory is aligned
    KASSERT(ram_stealmem(0) % PAGE_SIZE == 0);
    first_page = ADDR_TO_PAGE(ram_stealmem(0));
    last_page = ADDR_TO_PAGE(ram_getsize());

    // allocate physical page entries in the coremap
    size_t pages_used = first_page;
    for(p_page_t p_page = 0; p_page < pages_used; p_page++) {
        v_page_t v_page = PPAGE_TO_KVPAGE(p_page);
        cm->cm_entries[p_page] = 0 | PP_USED | v_page;
        cm->cm_counter++;
    }
}

vaddr_t alloc_kpages(size_t npages)
{
    spinlock_acquire(&cm->cm_spinlock);
    KASSERT(spinlock_do_i_hold(&cm->cm_spinlock));
    // find free physical addresses by incrementing start
    p_page_t start = first_page;
    while (start < last_page - npages) {
        if (!p_page_used(start)) {
            size_t offset;
            for (offset = 0; offset < npages; offset++) {
                if (p_page_used(start + offset)) {
                    break;
                }
            }

            if (offset == npages) {
                break;
            }
        }

        (start)++;
    }
    
    // check that there are physical addresses to allocate
    if (start == last_page - npages) {
        spinlock_release(&cm->cm_spinlock);
        return 0;   
    }

    // allocate npages of physical address starting from start
    for (p_page_t p_page = start; p_page < start + npages; p_page++) {
        v_page_t v_page = PPAGE_TO_KVPAGE(p_page);
        cm->cm_entries[p_page] = 0 | PP_USED | v_page;
        cm->cm_counter++;
    }

    // flag the last page in the coremap as the end of kmalloc 
    cm->cm_entries[start + npages - 1] = cm->cm_entries[start + npages - 1] | KMALLOC_END;
    spinlock_release(&cm->cm_spinlock);

    return PADDR_TO_KVADDR(PAGE_TO_ADDR(start));
}

void free_kpages(vaddr_t addr)
{
    p_page_t curr = ADDR_TO_PAGE(KVADDR_TO_PADDR(addr));

    spinlock_acquire(&cm->cm_spinlock);
    
    // frees pages until it finds an entry with the kmalloc_end flag
    while (p_page_used(curr)) {
        bool end = cm->cm_entries[curr] & KMALLOC_END;

        // free the page
        KASSERT(first_page <= curr && curr < last_page);
        cm->cm_entries[curr] = 0;
        cm->cm_counter--;

        if (end) {
            spinlock_release(&cm->cm_spinlock);
            return;
        }
        curr++;
    }

    spinlock_release(&cm->cm_spinlock);
}

void
vm_tlbshootdown_all()
{
    // marks all tlb entries as invalid
    for (int i = 0; i < NUM_TLB; i++){
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
}


void
vm_tlbshootdown(const struct tlbshootdown *tlbsd)
{
    // marks specific tlb entries as invalid up to index
    vaddr_t v_page = tlbsd->v_page_num;
    pid_t pid = tlbsd->pid;
    uint32_t entryhi = 0 | (v_page & PAGE_FRAME) | pid << 6;
    int32_t index = tlb_probe(entryhi, 0);

    if (index > -1) {
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    }
}
