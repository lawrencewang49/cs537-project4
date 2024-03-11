#include "wmap.h"
#include "types.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "memlayout.h"

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
            if ((addr >= mappings[i].addr && addr < mapping_end) || (addr + length >= mappings[i].addr && addr + length < mapping_end) || (mappings[i].addr >= addr && mappings[i].addr < addr + length) || (mapping_end >= addr && mapping_end < addr + length)) {
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
    if (curproc == 0) {
        return FAILED;
    }
    pdinfo->n_upages = 0;
    pde_t *curr_pgdir = curproc->pgdir;
    pte_t *pte;
    uint va = 0x60000000;
    int pdinfo_ind = 0;
    for (int i = 0; i < MAX_UPAGE_INFO; i++) {
        pte = walkpgdir(curr_pgdir, (void*)va, 0);
        if (pte && (*pte & PTE_U)) {
            pdinfo->n_upages++;
            pdinfo->va[pdinfo_ind] = va;
            pdinfo->pa[pdinfo_ind] = V2P(*pte);
            pdinfo_ind++;
        }
        va += PGSIZE;
    }
    return SUCCESS;
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
    return SUCCESS;
}

int handle_pagefault(uint addr) {
    for (int i = 0; i < num_mappings; i++) {
        if (addr >= mappings[i].addr || addr < mappings[i].addr + mappings[i].length) {
            struct proc *curr_proc = myproc();
            char *mem = kalloc();
            if (mem == 0) {
                return 0;
            }
            int success = mappages(curr_proc->pgdir, (void *)addr, PGSIZE, V2P(mem), PTE_W | PTE_U);
            if (success != 0) {
                kfree(mem);
                return 0;
            } else {
                return 1;
            }
        }
    }
    return 0;
}
