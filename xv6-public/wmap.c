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
    uint new_addr = 0x60000000;
    if (flags & MAP_FIXED) {
        if (addr < 0x60000000 || addr >= 0x80000000 || ((addr & (PGSIZE - 1)) != 0)) {
            return FAILED;
        }
        for (int i = 0; i < num_mappings; i++) {
            uint mapping_end = mappings[i].addr + mappings[i].length;
            if ((addr >= mappings[i].addr && addr < mapping_end) || (addr + length >= mappings[i].addr && addr + length < mapping_end)) {
                return FAILED;
            }
        }
        new_addr = addr;
    } else {
        for (int i = 0; i < num_mappings; i++) {
            uint mapping_end = mappings[i].addr + mappings[i].length;
            if ((new_addr >= mappings[i].addr && new_addr < mapping_end) || (new_addr + length >= mappings[i].addr && new_addr + length < mapping_end) || (mappings[i].addr >= new_addr && mappings[i].addr < new_addr + length) || (mapping_end >= new_addr && mapping_end < new_addr + length)) {
                new_addr = PGROUNDDOWN(mapping_end) + PGSIZE;
            }
        }
    }
    struct mapping new_mapping;
    new_mapping.addr = new_addr;
    new_mapping.length = length;
    new_mapping.flags = flags;
    new_mapping.fd = fd;
    mappings[num_mappings++] = new_mapping;
    return new_mapping.addr;
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
    wminfo->total_mmaps = 0;
    for (int i = 0; i < num_mappings; i++) {
        wminfo->total_mmaps++;
        wminfo->addr[i] = mappings[i].addr;
        wminfo->length[i] = mappings[i].length;
        uint num_pages = mappings[i].length / PGSIZE;
        if (mappings[i].length % PGSIZE != 0) {
            num_pages++;
        }
        wminfo->n_loaded_pages[i] = num_pages;
    }
    return 0;
}

