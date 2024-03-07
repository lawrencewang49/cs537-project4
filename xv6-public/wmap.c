#include "wmap.h"
#include "types.h"

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
        if ((addr < 0x60000000 || addr >= 0x80000000) || ((addr & (PAGE_SIZE - 1)) != 0)){
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

}

uint wremap(uint oldaddr, int oldsize, int newsize, int flags) {

}

int getpgdirinfo(struct pgdirinfo *pdinfo) {
    
}

int getwmapinfo(struct wmapinfo *wminfo) {

}
