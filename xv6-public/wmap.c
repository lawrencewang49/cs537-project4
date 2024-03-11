#include "wmap.h"
#include "types.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "fs.h"
#include "file.h"

void selectionSort(struct mapping arr[], int n) {
    int i, j, minIndex;
    struct mapping temp;
    for (i = 0; i < n - 1; i++) {
        minIndex = i;
        for (j = i + 1; j < n; j++) {
            // Compare based on the 'addr' field
            if (arr[j].addr < arr[minIndex].addr) {
                minIndex = j;
            }
        }
        // Swap arr[i] and arr[minIndex]
        temp = arr[i];
        arr[i] = arr[minIndex];
        arr[minIndex] = temp;
    }
}

uint wmap(uint addr, int length, int flags, int fd) {
    struct proc *curproc = myproc();
    // Have we reached the max number of mappings?
    // Is the length that the user provides > 0?
    if (curproc->num_mappings >= MAX_WMMAP_INFO || length <= 0 || ((curproc->mappings->flags & MAP_PRIVATE) && (curproc->mappings->flags & MAP_SHARED))) {
        return FAILED;
    }
    uint new_addr = 0x60000000;
    if (flags & MAP_FIXED) { // Consider case of fixed addr
        // Check that addr fits within WMAP space + is page aligned
        if (addr < 0x60000000 || addr >= 0x80000000 || addr % PGSIZE != 0) {
            return FAILED;
        }
        // Iterate through mappings to find appropriate region to insert new region
        for (int i = 0; i < curproc->num_mappings; i++) {
            uint mapping_end = curproc->mappings[i].addr + (uint) curproc->mappings[i].length;
            if (
                (addr >= curproc->mappings[i].addr && addr < mapping_end) || // Does the start of the region overlap with current region
                (addr + (uint) length >= curproc->mappings[i].addr && addr + (uint) length < mapping_end)||
                (curproc->mappings[i].addr >= addr && curproc->mappings[i].addr < addr + (uint) length) || 
                (mapping_end >= addr && mapping_end < addr + (uint) length)
            ) {
                return FAILED;
            }
        }
        new_addr = addr;
    } else {
        for (int i = 0; i < curproc->num_mappings; i++) {
            uint mapping_end = curproc->mappings[i].addr + (uint) curproc->mappings[i].length;
            if ((new_addr >= curproc->mappings[i].addr && new_addr < mapping_end) || (new_addr + (uint) length >= curproc->mappings[i].addr && new_addr + (uint) length < mapping_end) || (curproc->mappings[i].addr >= new_addr && curproc->mappings[i].addr < new_addr + (uint) length) || (mapping_end >= new_addr && mapping_end < new_addr + (uint) length)) {
                new_addr = PGROUNDUP(mapping_end);
            }
        }
    }
    struct mapping new_mapping;
    new_mapping.addr = new_addr;
    new_mapping.length = length;
    new_mapping.flags = flags;
    new_mapping.fd = fd;
    new_mapping.num_pages_loaded = 0;
    curproc->mappings[curproc->num_mappings++] = new_mapping;
    selectionSort(curproc->mappings, curproc->num_mappings);
    return new_mapping.addr;
}

int wunmap(uint addr) {
    struct proc *curproc = myproc();
    if (addr % PGSIZE != 0) {
        return FAILED;
    }
    int found = 0;
    for (int i = 0; i < curproc->num_mappings; i++) {
        if (curproc->mappings[i].addr == addr) {
            uint start = PGROUNDDOWN(curproc->mappings[i].addr);
            uint end = PGROUNDUP(curproc->mappings[i].addr + curproc->mappings[i].length);
            found = 1;
            uint anon = curproc->mappings[i].flags & MAP_ANONYMOUS;
            uint shared = curproc->mappings[i].flags & MAP_SHARED;
            // Check for file backing
            if (shared && !anon) {
                struct file *f = curproc->ofile[curproc->mappings[i].fd];
                f->off = 0;
                if (filewrite(f, (char *) start, curproc->mappings[i].length) < 0){
                    cprintf("filewrite failed\n");
                    return FAILED; 
                }
            }
            for (uint start_addr = start; start_addr < end; start_addr += PGSIZE) {
                pte_t *pte = walkpgdir(curproc->pgdir, (void *)start_addr, 0);
                if (pte && (*pte & PTE_P)) {
                    kfree(P2V(PTE_ADDR(*pte)));
                    *pte = 0;
                }
            }
            // Shift all subsequent mappings one position towards the start
            for (int j = i; j < curproc->num_mappings; j++) {
                curproc->mappings[j] = curproc->mappings[j + 1];
            }
            curproc->num_mappings--;
            break;
        }
    }
    if (!found) {
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
    uint va = 0;
    int user_allocated_pages = 0;
    pdinfo->n_upages = 0;
    // cprintf("1\n");
    while (user_allocated_pages < MAX_UPAGE_INFO && va < KERNBASE) {
        pte = walkpgdir(curr_pgdir, (void*)va, 0);
        if (pte && (*pte & PTE_U)) {
            // cprintf("0x%x, %x\n", va, (*pte));
            // cprintf("enters if statement\n");
            pdinfo->n_upages++;
            // cprintf("user pages %d\n", pdinfo->n_upages);
            pdinfo->va[user_allocated_pages] = va;
            pdinfo->pa[user_allocated_pages] = PTE_ADDR(*pte);
            user_allocated_pages++;
        }
        va += PGSIZE;
    }
    // cprintf("2\n");
    return SUCCESS;
}

int getwmapinfo(struct wmapinfo *wminfo) {
    struct proc *curproc = myproc();
    wminfo->total_mmaps = curproc->num_mappings;
    for (int i = 0; i < curproc->num_mappings; i++) {
        wminfo->addr[i] = curproc->mappings[i].addr;
        wminfo->length[i] = curproc->mappings[i].length;
        wminfo->n_loaded_pages[i] = curproc->mappings[i].num_pages_loaded;
    }
    return SUCCESS;
}

int handle_pagefault(uint addr) {
    struct proc *curproc = myproc();
    for (int i = 0; i < curproc->num_mappings; i++) {
        if (addr >= curproc->mappings[i].addr && addr < curproc->mappings[i].addr + curproc->mappings[i].length) {
            char *mem = kalloc();
            int success;
            if (mem == 0) {
                return 0;
            }
            curproc->mappings[i].num_pages_loaded++;
            if (curproc->mappings[i].flags & MAP_ANONYMOUS) {
                success = mappages(curproc->pgdir, (void *)addr, PGSIZE, V2P(mem), PTE_W | PTE_U);
                if (success != 0) {
                    kfree(mem);
                    return 0;
                } else {
                    memset(mem, 0, PGSIZE);
                    return 1;
                }
            } else {
                // file-backed mapping
                struct file *f = curproc->ofile[curproc->mappings[i].fd];
                ilock(f->ip);
                readi(f->ip, mem, addr - curproc->mappings[i].addr, PGSIZE);
                iunlock(f->ip);
                success = mappages(curproc->pgdir, (void *)addr, PGSIZE, V2P(mem), PTE_W | PTE_U);
                if (success != 0) {
                    kfree(mem);
                    return 0;
                } else {
                    return 1;
                }
            }
        }
    }
    return 0;
}
