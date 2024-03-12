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
    if (curproc->num_mappings >= MAX_WMMAP_INFO || length <= 0 || ((flags & MAP_PRIVATE) && (flags & MAP_SHARED))) {
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
            uint mapping_end = curproc->mappings[i].addr + curproc->mappings[i].length;
            if (
                (addr >= curproc->mappings[i].addr && addr < mapping_end) || // Does the start of the region overlap with current region
                (addr + length - 1>= curproc->mappings[i].addr && addr + length - 1< mapping_end) ||
                (curproc->mappings[i].addr >= addr && curproc->mappings[i].addr < addr + length - 1) || 
                (mapping_end >= addr && mapping_end < addr + length - 1)
            ) {
                return FAILED;
            }
        }
        new_addr = addr;
    } else {
        // Also iterate, but find next available page instead of failing
        for (int i = 0; i < curproc->num_mappings; i++) {
            uint mapping_end = curproc->mappings[i].addr + curproc->mappings[i].length;
            if ((new_addr >= curproc->mappings[i].addr && new_addr < mapping_end) || (new_addr + length - 1 >= curproc->mappings[i].addr && new_addr + length - 1 < mapping_end) || (curproc->mappings[i].addr >= new_addr && curproc->mappings[i].addr < new_addr + length - 1) || (mapping_end >= new_addr && mapping_end < new_addr + length - 1)) {
                new_addr = PGROUNDUP(mapping_end);
            }
        }
        // Check if length is in bounds
        if (new_addr + length - 1 >= KERNBASE) {
            return FAILED;
        }
    }
    // Make the mapping
    struct mapping new_mapping;
    new_mapping.addr = new_addr;
    new_mapping.length = length;
    new_mapping.flags = flags;
    new_mapping.fd = fd;
    new_mapping.num_pages_loaded = 0;
    curproc->mappings[curproc->num_mappings++] = new_mapping;
    // Sort the mappings to make it easier to perform other operations
    selectionSort(curproc->mappings, curproc->num_mappings);
    return new_mapping.addr;
}

int wunmap(uint addr) {
    struct proc *curproc = myproc();
    if (addr % PGSIZE != 0) {
        return FAILED;
    }
    int found = 0;
    // Iterate to find correct page to unmap
    for (int i = 0; i < curproc->num_mappings; i++) {
        if (curproc->mappings[i].addr == addr) {
            uint start = curproc->mappings[i].addr;
            uint end = PGROUNDUP(curproc->mappings[i].addr + curproc->mappings[i].length);
            found = 1;
            // Check if shared + file backed --> write to file
            uint anon = curproc->mappings[i].flags & MAP_ANONYMOUS;
            uint shared = curproc->mappings[i].flags & MAP_SHARED;
            if (shared && !anon) {
                struct file *f = curproc->ofile[curproc->mappings[i].fd];
                f->off = 0;
                // copy it over
                if (filewrite(f, (char *) start, curproc->mappings[i].length) < 0){
                    cprintf("filewrite failed\n");
                    return FAILED; 
                }
            }
            // remove pages from physical memory
            for (uint start_addr = start; start_addr < end; start_addr += PGSIZE) {
                pte_t *pte = walkpgdir(curproc->pgdir, (void *)start_addr, 0);
                if (pte && (*pte & PTE_P)) {
                    kfree(P2V(PTE_ADDR(*pte)));
                    *pte = 0;
                }
            }
            // Shift all subsequent mappings one position towards the start to remove from virtual memory
            for (int j = i; j < curproc->num_mappings; j++) {
                curproc->mappings[j] = curproc->mappings[j + 1];
            }
            curproc->num_mappings--;
            break;
        }
    }
    // Check if we actually removed a page
    if (!found) {
        return FAILED;
    }
    return SUCCESS;
}

uint wremap(uint oldaddr, int oldsize, int newsize, int flags) {
    if (oldaddr % PGSIZE != 0 || newsize <= 0){
        return FAILED;
    }
    struct proc *curproc = myproc();
    int found = 0;
    int i;  // get mapping that we want to remap
    for(i = 0; i < curproc->num_mappings; i++){
        if(curproc->mappings[i].addr == oldaddr){
            found = 1;
            break;
        }
    }
    if (!found) {
        return FAILED;
    }
    // 3 cases, same size, smaller size (shrink), larger size (expand)
    int diff = newsize - oldsize;
    // Case 1: same size --> do nothing, keep same address
    if (diff == 0) {
        return oldaddr;
    } else if (diff < 0) { // Case 2: shrinks mapping
        // set the new size, if shrinking can always stay at current address
        curproc->mappings[i].length = newsize;
        int temp = oldsize;
        // remove pages from memory as a result of shrinking
        while (temp > newsize) {
            pte_t *pte = walkpgdir(curproc->pgdir, (void *)oldaddr + temp - PGSIZE, 0);
            if (pte && (*pte & PTE_P)) {
                kfree(P2V(PTE_ADDR(*pte)));
                *pte = 0;
            }
            temp -= PGSIZE;
        }
        return oldaddr;
    } else { // Case 3: larger size, find other address to move mapping
        uint end = oldaddr + newsize - 1;
        // If newsize is out of bounds, fail
        if (end >= KERNBASE) {
            return FAILED;
        }
        // If in bounds and is the last mapping, simply expand size and return current address
        if (i == curproc->num_mappings - 1) {
            curproc->mappings[i].length = newsize;
            return oldaddr;
        }
        // Checks if we can move mapping
        if (flags == 0) { // Can't move mapping
            // checks if there is more than 1 mapping, and if the newsize overlaps with the other mapping
            if (curproc->num_mappings >= 2 && i < curproc->num_mappings - 1) {
                uint next_map_end = curproc->mappings[i + 1].addr;
                if (end >= next_map_end) {
                    return FAILED;
                }
            }
            curproc->mappings[i].length = newsize;
            return oldaddr;
        } else { // Can move mapping
            uint next_map_end = curproc->mappings[i + 1].addr;
            // We need to move the mapping
            if (end >= next_map_end) {
                // Find the next available address
                uint new_addr = 0x60000000;
                for (int i = 0; i < curproc->num_mappings; i++) {
                    uint mapping_end = curproc->mappings[i].addr + curproc->mappings[i].length;
                    if ((new_addr >= curproc->mappings[i].addr && new_addr < mapping_end) || (new_addr + newsize - 1 >= curproc->mappings[i].addr && new_addr + newsize - 1 < mapping_end) || (curproc->mappings[i].addr >= new_addr && curproc->mappings[i].addr < new_addr + newsize - 1) || (mapping_end >= new_addr && mapping_end < new_addr + newsize - 1)) {
                        new_addr = PGROUNDUP(mapping_end);
                    }
                }
                // Check if in bounds
                if (new_addr + newsize - 1 >= KERNBASE) {
                    return FAILED;
                }
                // Allocate at new address
                struct mapping new_mapping;
                new_mapping.addr = new_addr;
                new_mapping.length = newsize;
                new_mapping.flags = curproc->mappings[i].flags;
                new_mapping.fd = curproc->mappings[i].fd;
                new_mapping.num_pages_loaded = curproc->mappings[i].num_pages_loaded;
                curproc->mappings[i] = new_mapping;
                // remove form old address
                while (oldsize > 0) {
                    pte_t *pte = walkpgdir(curproc->pgdir, (void *)(oldaddr + oldsize - PGSIZE), 0);
                    if (*pte & PTE_P) {
                        mappages(curproc->pgdir, (void *)(new_addr + oldsize - PGSIZE), PGSIZE, PTE_ADDR(*pte), PTE_W | PTE_U);
                        *pte = 0;
                    }
                    oldsize -= PGSIZE;
                }
                wunmap(oldaddr);
                // sort mappings to make it easier to perform other operations
                selectionSort(curproc->mappings, curproc->num_mappings);
                return new_mapping.addr;
            } else {
                // if it doesn't overlap (can stay at current address), simply expand the mapping size and return old address
                curproc->mappings[i].length = newsize;
                return oldaddr;
            }
        }
    }
    // didn't find anything to remap
    return FAILED;
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
    // loops through memory to find all user allocated pages within bounds, increments count and stores page size
    while (user_allocated_pages < MAX_UPAGE_INFO && va < KERNBASE) {
        pte = walkpgdir(curr_pgdir, (void*)va, 0);
        if (pte && (*pte & PTE_U)) {
            pdinfo->n_upages++;
            pdinfo->va[user_allocated_pages] = va;
            pdinfo->pa[user_allocated_pages] = PTE_ADDR(*pte);
            user_allocated_pages++;
        }
        va += PGSIZE;
    }
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
    // Finds correct page that faults by looping
    for (int i = 0; i < curproc->num_mappings; i++) {
        if (addr >= curproc->mappings[i].addr && addr < curproc->mappings[i].addr + curproc->mappings[i].length) {
            // Allocate memory w/ kalloc()
            char *mem = kalloc();
            int success;
            if (mem == 0) {
                return 0;
            }
            // lazy allocation: load a page into physical memory
            curproc->mappings[i].num_pages_loaded++;
            // If anon map, simply map pages
            if (curproc->mappings[i].flags & MAP_ANONYMOUS) {
                success = mappages(curproc->pgdir, (void *)addr, PGSIZE, V2P(mem), PTE_W | PTE_U);
                if (success != 0) {
                    kfree(mem);
                    return 0;
                } else {
                    // only allocate page faulted address page
                    memset(mem, 0, PGSIZE);
                    return 1;
                }
            } else {
                // file-backed mapping
                struct file *f = curproc->ofile[curproc->mappings[i].fd];
                // read contents
                ilock(f->ip);
                readi(f->ip, mem, addr - curproc->mappings[i].addr, PGSIZE);
                iunlock(f->ip);
                // map contents
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
    // didn't find a page to allocate
    return 0;
}
