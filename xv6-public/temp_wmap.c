#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"
#include "wmap.h"

#define USERBOUNDARY 0x60000000
#define KERNBASE 0x80000000


// struct mem_mapping {
//     uint addr;
//     int length;
//     int flags;
//     int fd;
//     int valid;
//     int n_pages_loaded;
// };

// // should be maintained in sorted order by addr
// static struct mem_mapping mappings[MAX_WMMAP_INFO];
// static int num_mappings = 0;

uint getValidAddress(int length) {

    struct proc *currproc = myproc();

    if(currproc -> num_mappings == 0) {
        return USERBOUNDARY;
    }
    uint addr = USERBOUNDARY;
    for(int i = 0; i < currproc -> num_mappings; i++) {
        struct mem_mapping *mapping = &(currproc -> mappings[i]);
        if(mapping->valid && addr + length <= mapping->addr) {
            return addr;
        }
        addr = mapping->addr + mapping->length;
        addr = PGROUNDUP(addr);
    }
    if(addr + length <= KERNBASE) {
        return addr;
    }
    return 0;
}

int isAddressValid(uint addr) { 
    if(addr < USERBOUNDARY || addr >= KERNBASE || addr % PGSIZE != 0) {
        return 0;
    }

    struct proc *currproc = myproc();

    for(int i = 0; i < currproc -> num_mappings; i++) {
        struct mem_mapping *mapping = &(currproc -> mappings[i]);
        if(mapping->valid && addr >= mapping->addr && addr < mapping->addr + mapping->length) {
            return 0;
        }
    }
    return 1;
}

void insertMapping(struct mem_mapping *mapping) {
    // insert in sorted order and shift all elements to the right
    struct proc *currproc = myproc();

    int i = currproc -> num_mappings;
    while(i > 0 && currproc -> mappings[i-1].addr > mapping->addr) {
        currproc -> mappings[i] = currproc -> mappings[i-1];
        i--;
    }
    currproc -> mappings[i] = *mapping;
    currproc -> num_mappings++;
}

void unmap_page(pde_t *pgdir, uint va, struct mem_mapping *mapping) {
   
    // delete the pte
    pte_t *pte = walkpgdir(pgdir, (void *)va, 0);

    if (pte == 0 || !(*pte & PTE_P)) {
        return;
    }

    if(!(mapping->flags & MAP_ANONYMOUS) && !(mapping->flags & MAP_PRIVATE)) {
        struct file *file = myproc()->ofile[mapping->fd];
        if (file == 0) {
            return; // File descriptor is not valid
        }

        file->off = va - mapping->addr;
        // calculate number of bytes to write, max is PGSIZE
        int n_bytes = mapping->addr + mapping->length - va;
        if (n_bytes > PGSIZE) {
            n_bytes = PGSIZE;
        }
        if (filewrite(file, (char *)va, n_bytes) < 0){
            panic("unmap_page: filewrite failed\n");
            return; // Unable to write to file
        }
    }
    kfree((char *)P2V(PTE_ADDR(*pte)));
    *pte = 0;
    cprintf("unmap_page: va: %x\n", va);
    
}

int deleteMapping(uint addr) {
    cprintf("deleteMapping: addr: %x\n", addr);

    struct proc *currproc = myproc();

    int found = 0;
    int i;
    for( i = 0; i < currproc -> num_mappings; i++) {
        struct mem_mapping *mapping = &(currproc -> mappings[i]);
        if(mapping->valid && mapping->addr == addr) {
            found = 1;
            break;
        }
    }

    if(found){
        //Shift left

        int initAddr = currproc -> mappings[i].addr;

        while(initAddr < currproc -> mappings[i].addr+currproc -> mappings[i].length){
            pde_t *pte = currproc -> pgdir;

            unmap_page(pte, initAddr, &(currproc -> mappings[i]));

            initAddr = initAddr + PGSIZE;
        }


        for(int j = i; j < currproc -> num_mappings - 1; j++){
            currproc -> mappings[j] = currproc -> mappings[j+1];
        }
        currproc -> num_mappings--;

        return 0;
    }
    

    return -1;
}

uint wmap(uint addr, int length, int flags, int fd) {
    /*
    if(flags & MAP_FIXED)
        cprintf("wmap: MAP_FIXED\n");
    if(flags & MAP_SHARED)
        cprintf("wmap: MAP_SHARED\n");
    if(flags & MAP_PRIVATE)
        cprintf("wmap: MAP_PRIVATE\n");
    if(flags & MAP_ANONYMOUS)
        cprintf("wmap: MAP_ANONYMOUS\n");
    */

    if(length <= 0){
       return FAILED;
    }

    if ((flags & MAP_SHARED) && (flags & MAP_PRIVATE)) {
        return FAILED; // Both MAP_SHARED and MAP_PRIVATE flags cannot be set together
    }

    if(!isAddressValid(addr)){
        if(flags & MAP_FIXED){
            return FAILED;
        }
        addr = getValidAddress(length);
    }
    cprintf("wmap: addr: %x, length: %d, flags: %d, fd: %d\n", addr, length, flags, fd);

    struct mem_mapping mapping;
    mapping.addr = addr;
    mapping.length = length;
    mapping.flags = flags;
    mapping.fd = fd;
    mapping.valid = 1;
    mapping.n_pages_loaded = 0;

    insertMapping(&mapping);
    
    return addr;
}

int allocatePage(uint addr, struct mem_mapping *mapping) {
    char *mem = kalloc();
    if (mem == 0) {
        cprintf("allocatePage: mem is 0\n");
        return 0; // Unable to allocate physical page
    }

    // perform mapping
    uint addrStart = PGROUNDDOWN(addr);
    cprintf("allocatePage: addrStart: %x, physMem: %x\n", addrStart, V2P(mem));
    if (mappages(myproc()->pgdir, (void *)addrStart, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0) {
        kfree(mem);
        cprintf("allocatePage: mappages failed\n");
        return 0; // Unable to map virtual address to physical page
    }

    // pte_t *pte = walkpgdir(myproc()->pgdir, (void *)addrStart, 0);
    // cprintf("allocatePage: pte: %x\n", *pte);

    // set page contents
    if (mapping->flags & MAP_ANONYMOUS) {
        // cprintf("allocatePage: MAP_ANONYMOUS\n");
        memset(mem, 0, PGSIZE);
    } 
    else {
        struct file *file = myproc()->ofile[mapping->fd];
        if (file == 0) {
            kfree(mem);
            return 0; // File descriptor is not valid
        }

        file->off = addrStart - mapping->addr;
        int n_bytes = mapping->addr + mapping->length - addrStart;
        if (n_bytes > PGSIZE) {
            n_bytes = PGSIZE;
        }
        if (fileread(file, mem, n_bytes) < 0){
            kfree(mem);
            panic("allocatePage: fileread failed\n");
            return 0; // Unable to read from file
        }
    }
    mapping->n_pages_loaded++;
    return 1; // Page allocated successfully
}


// TODO: optimize this function knowing that the mappings are sorted by address. We don't have to check all memory mappings
int wmap_handle_page_fault(uint addr) {

    struct proc *currproc = myproc();

    for (int i = 0; i < currproc -> num_mappings; i++) {
        struct mem_mapping *mapping = &(currproc -> mappings[i]);
        if (!mapping->valid || addr < mapping->addr || addr >= mapping->addr + mapping->length) {
            continue;
        }
        return allocatePage(addr, mapping);
    }
    return 0; // Page fault not due to memory mapping
}

int wunmap(uint addr){
    return deleteMapping(addr);
}

uint wremap(uint oldaddr, int oldsize, int newsize, int flags){
    cprintf("Wremap\n");
    if(oldaddr % PGSIZE != 0){
        return FAILED;
    }

    struct proc * currproc = myproc();
    if(currproc == 0){
        return FAILED;
    }

    int found = 0;
    int i = 0;
    for(i = 0; i < currproc -> num_mappings; i++){
        if(currproc -> mappings[i].addr == oldaddr){
            found = 1;
            break;
        }
    }

    if(!found){
        return FAILED;
    }

    int sizeDiff = newsize - oldsize;
    if(sizeDiff == 0){
        return oldaddr;
    }else if(sizeDiff > 0){
        int valid = 1;
        cprintf("A\n");
        if(i+1 == currproc -> num_mappings) {
            if(oldaddr + newsize > KERNBASE ){
                cprintf("B\n");
                valid = 0;
            }
        }
        else if((currproc -> num_mappings >= 2 && oldaddr + newsize > currproc->mappings[i+1].addr) ){
            
             cprintf("C\n");
            valid = 0;
        }
        if(valid) {
            cprintf("D\n");
            currproc -> mappings[i].length = newsize;
            return oldaddr;
        }
        cprintf("E\n");

        if(flags == 0){
            return FAILED;
        }else{
            cprintf("F\n");
            uint newaddr = getValidAddress(newsize);
            if (newaddr == FAILED) {
                return FAILED; // Unable to find valid address for new mapping
            }

            struct mem_mapping new_mapping;
            new_mapping.addr = newaddr;
            new_mapping.length = newsize;
            new_mapping.flags = currproc->mappings[i].flags;
            new_mapping.fd = currproc->mappings[i].fd;
            new_mapping.valid = 1;
            new_mapping.n_pages_loaded = currproc->mappings[i].n_pages_loaded;

            cprintf("G\n");
            //currproc -> mappings[i] = new_mapping;
            while (oldsize > 0) {
                uint pageOffset = oldsize - PGSIZE;
                pte_t *pte = walkpgdir(currproc->pgdir, (void *)(pageOffset + oldaddr), 0);

                if(!(PTE_P & *pte))
                    goto next;

                mappages(currproc->pgdir, (void *)(newaddr + pageOffset), PGSIZE, PTE_ADDR(*pte), PTE_W | PTE_U);
                //unmap_page(currproc->pgdir, oldaddr + pageOffset, &(currproc->mappings[i]));
                *pte = 0;
                
                next:
                oldsize -= PGSIZE;
            }
            cprintf("H\n");

            //unmappages(currproc->pgdir, oldaddr,  oldsize, &(currproc->mappings[i]));
            deleteMapping(oldaddr);
            cprintf("J\n");
            insertMapping(&new_mapping);
            cprintf("K\n");

            return newaddr;
            
        }

    }else{

        currproc -> mappings[i].length = newsize;
        
        //uint oldEndAddr = oldaddr + oldsize;

        while(oldsize > newsize){
            unmap_page(currproc->pgdir, oldaddr + oldsize - PGSIZE, &(currproc->mappings[i]));
            oldsize = oldsize - PGSIZE;
        }
        
        //deleteMapping(oldaddr);

        return oldaddr;
    }

    return FAILED;
}


int getpgdirinfo(struct pgdirinfo *pdinfo) {
    struct proc *curproc = myproc();
    if (curproc == 0)
        return FAILED; // No current process

    if (argptr(0, (char **)&pdinfo, sizeof(struct pgdirinfo)) < 0) {
        return FAILED; // Error in retrieving arguments
    }

    pdinfo->n_upages = 0;

    // Initialize arrays to 0
    for (int i = 0; i < MAX_UPAGE_INFO; i++) {
        pdinfo->va[i] = 0;
        pdinfo->pa[i] = 0;
    }

    pde_t *pgdir = curproc->pgdir;
    uint va = 0;
    int i = 0;

    while (i < MAX_UPAGE_INFO) {
        pte_t *pte = walkpgdir(pgdir, (void *)va, 0);

        if (pte != 0 && (*pte & PTE_P) != 0 && (*pte & PTE_U) != 0) {
            pdinfo->va[i] = va;
            pdinfo->pa[i] = PTE_ADDR(*pte);
            pdinfo->n_upages++;
            i++;
        }
        va += PGSIZE;
    }

    return SUCCESS; // Success
}

int getwmapinfo(struct wmapinfo *wminfo) {

    struct proc *curproc = myproc();

    wminfo->total_mmaps = curproc -> num_mappings;
    for(int i = 0; i < curproc -> num_mappings; i++) {
        struct mem_mapping *mapping = &(curproc -> mappings[i]);
        if(mapping->valid) {
            wminfo->addr[i] = mapping->addr;
            wminfo->length[i] = mapping->length;
            wminfo->n_loaded_pages[i] = mapping->n_pages_loaded;
        }
    }
    return 0;
}
