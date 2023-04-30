#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memlib.h"
#include "mm.h"
#include "mymalloc.h"

void myinit(int allocAlg) {
    mem_init();
    mm_init(allocAlg);
}

void* mymalloc(size_t size) {
    return mm_malloc(size);
}

void myfree(void* ptr) {
    mm_free(ptr);
}

void* myrealloc(void* ptr, size_t size) {
    return mm_realloc(ptr, size);
}

void mycleanup() {
    mem_deinit();
}
