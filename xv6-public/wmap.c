#include "wmap.h"
#include "types.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "fs.h"
#include "file.h"

struct mapping {
    uint addr;
    int length; 
    int flags;
    int fd;
};

struct mapping mappings[MAX_WMMAP_INFO];
int num_mappings = 0;

uint wmap(uint addr, int length, int flags, int fd) {
    // Have we reached the max number of mappings?
    // Is the length that the user provides > 0?
    if (num_mappings >= MAX_WMMAP_INFO || length <= 0) {
        return FAILED;
    }

    uint new_addr = 0x60000000;
    if (flags & MAP_FIXED) { // Consider case of fixed addr
        // Check that addr fits within WMAP space + is page aligned
        if (addr < 0x60000000 || addr >= 0x80000000 || addr % PGSIZE != 0) {
            return FAILED;
        }

        // Iterate through mappings to find appropriate region to insert new region
        for (int i = 0; i < num_mappings; i++) {
            uint mapping_end = mappings[i].addr + (uint) mappings[i].length;
            if (
                (addr >= mappings[i].addr && addr < mapping_end) || // Does the start of the region overlap with current region
                (addr + (uint) length >= mappings[i].addr && addr + (uint) length < mapping_end)||
                (mappings[i].addr >= addr && mappings[i].addr < addr + (uint) length) || 
                (mapping_end >= addr && mapping_end < addr + (uint) length)
            ) {
                return FAILED;
            }
        }
        new_addr = addr;
    } else {
        for (int i = 0; i < num_mappings; i++) {
            uint mapping_end = mappings[i].addr + (uint) mappings[i].length;
            if ((new_addr >= mappings[i].addr && new_addr < mapping_end) || (new_addr + (uint) length >= mappings[i].addr && new_addr + (uint) length < mapping_end) || (mappings[i].addr >= new_addr && mappings[i].addr < new_addr + (uint) length) || (mapping_end >= new_addr && mapping_end < new_addr + (uint) length)) {
                new_addr = PGROUNDUP(mapping_end);
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
    pde_t *curr_pgdir = curproc->pgdir;
    pte_t *pte;
    uint va = 0x60000000;
    int user_allocated_pages = 0;
    pdinfo->n_upages = 0;
    // cprintf("n_upages: %d\n", pdinfo->n_upages);
    cprintf("1\n");
    while (user_allocated_pages < MAX_UPAGE_INFO && va < KERNBASE) {
        pte = walkpgdir(curr_pgdir, (void*)va, 0);
        cprintf("%d, %d\n", *pte, (*pte & PTE_U));
        if (pte && (*pte & PTE_U)) {
            cprintf("enters if statement\n");
            pdinfo->n_upages++;
            cprintf("user pages %d\n", pdinfo->n_upages);
            pdinfo->va[user_allocated_pages] = va;
            //cprintf("0x%x\n", pdinfo->va[user_allocated_pages]);
            pdinfo->pa[user_allocated_pages] = PTE_ADDR(*pte);
            //cprintf("0x%x\n", pdinfo->pa[user_allocated_pages]);
            // cprintf("pa: %x, va: %x\n", pdinfo->pa[user_allocated_pages], pdinfo->va[user_allocated_pages]);
            user_allocated_pages++;
            //cprintf("%d\n", user_allocated_pages);
        }
        va += PGSIZE;
    }
    cprintf("2\n");
    return SUCCESS;
}

int getwmapinfo(struct wmapinfo *wminfo) {
    wminfo->total_mmaps = 0;
    for (int i = 0; i < num_mappings; i++) {
        wminfo->total_mmaps++;
        //cprintf("%d\n", wminfo->total_mmaps);
        wminfo->addr[i] = mappings[i].addr;
        //cprintf("0x%x\n", wminfo->addr[i]);
        wminfo->length[i] = mappings[i].length;
        //cprintf("%d\n", wminfo->length[i]);
        uint num_pages = mappings[i].length / PGSIZE;
        if (mappings[i].length % PGSIZE != 0) {
            num_pages++;
        }
        wminfo->n_loaded_pages[i] = num_pages;
        //cprintf("%d\n", num_pages);
    }
    return SUCCESS;
}

int handle_pagefault(uint addr) {
    for (int i = 0; i < num_mappings; i++) {
        if (addr >= mappings[i].addr && addr < mappings[i].addr + mappings[i].length) {
            struct proc *curr_proc = myproc();
            char *mem = kalloc();
            int success;
            if (mem == 0) {
                return 0;
            }
            if (mappings[i].flags & MAP_ANONYMOUS) {
                success = mappages(curr_proc->pgdir, (void *)addr, PGSIZE, V2P(mem), PTE_W | PTE_U);
                if (success != 0) {
                    kfree(mem);
                    return 0;
                } else {
                    // cprintf("This was mapped anonymously\n");
                    return 1;
                }
            } else {
                // file-backed mapping
                struct file *f = curr_proc->ofile[mappings[i].fd];
                ilock(f->ip);
                readi(f->ip, mem, addr - mappings[i].addr, PGSIZE);
                iunlock(f->ip);
                success = mappages(curr_proc->pgdir, (void *)addr, PGSIZE, V2P(mem), PTE_W | PTE_U);
                if (success != 0) {
                    kfree(mem);
                    return 0;
                } else {
                    // cprintf("This was mapped w/ file backed\n");
                    return 1;
                }
            }
        }
    }
    return 0;
}
