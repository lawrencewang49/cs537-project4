#include "wmap.h"
#include "types.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"

struct mapping {
    uint addr;
    int length; 
    int flags;
    int fd;
};

struct mapping mappings[MAX_WMMAP_INFO];
int num_mappings = 0;

uint wmap(uint addr, int length, int flags, int fd) {
    if (num_mappings >= MAX_WMMAP_INFO || length <= 0) {
        return FAILED;
    }
    if (flags & MAP_FIXED) {
        if ((addr < 0x60000000 || addr >= 0x80000000) || ((addr & (PGSIZE - 1)) != 0)){
            return FAILED;
        }
        for (int i = 0; i < MAX_WMMAP_INFO; i++) {
            if (addr == mappings[i].addr) {
                return FAILED;
            }
        }
    }
    struct mapping new_mapping;
    new_mapping.addr = addr;
    new_mapping.length = length;
    new_mapping.flags = flags;
    new_mapping.fd = fd;
    mappings[num_mappings++] = new_mapping;
    return addr;
}

int wunmap(uint addr) {
    int found = 0;
    for (int i = 0; i < num_mappings; i++) {
        if (mappings[i].addr == addr) {
            found = 1;
            // Shift all subsequent mappings one position towards the start
            for (int j = i; j < num_mappings - 1; j++) {
                mappings[j] = mappings[j + 1];
            }
            num_mappings--; // Decrease the total number of mappings
            break; // Exit the loop once the mapping is found and handled
        }
    }
    if (!found) {
        // No mapping found for the given address
        return FAILED;
    }
    return SUCCESS;
}

uint wremap(uint oldaddr, int oldsize, int newsize, int flags) {
    return SUCCESS;
}

int getpgdirinfo(struct pgdirinfo *pdinfo) {
    struct proc *curproc = myproc();
    pdinfo->n_upages = 0;
    for (int i = 0; i < MAX_UPAGE_INFO && i < curproc->sz; i += PGSIZE) {
        // If the page is a user page
        if (curproc->pgdir[i / PGSIZE] & PTE_U) { 
            // Store the virtual and physical addresses of the page
            pdinfo->va[pdinfo->n_upages] = i;
            pdinfo->pa[pdinfo->n_upages] = PTE_ADDR(curproc->pgdir[i / PGSIZE]);
            pdinfo->n_upages++;
        }
    }
    return 0;
}

int getwmapinfo(struct wmapinfo *wminfo) {
    // struct proc *curproc = myproc();
    // wminfo->total_mmaps = 0;
    // for (int i = 0; i < MAX_WMMAP_INFO; i++) {
    //     // Store the address, length, and number of loaded pages for each memory map
    //     wminfo->total_mmaps++;
    //     wminfo->addr[i] = mappings[i].addr;
    //     wminfo->length[i] = mappings[i].length;
    //     wminfo->n_loaded_pages[i] = PGROUNDUP(mappings[i].length);
    // }
    struct proc *curproc = myproc();
    wminfo->total_mmaps = 0;
    for (int i = 0; i < wminfo->total_mmaps; i++) {
        // Store the address, length, and number of loaded pages for each memory map
        wminfo->addr[i] = curproc->wmaps[i].addr;
        wminfo->length[i] = curproc->wmaps[i].length;
        wminfo->n_loaded_pages[i] = curproc->wmaps[i].n_loaded_pages;
        wminfo->total_mmaps++;
    }
    return 0;
}

