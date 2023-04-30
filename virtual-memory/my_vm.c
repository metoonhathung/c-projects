#include "my_vm.h"

void* physical_mem = NULL;
pde_t* page_directory = NULL;
char* physical_bitmap = NULL;
char* virtual_bitmap = NULL;
int len_pt = 0;
int tlb_lookups = 0;
int tlb_misses = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* get_pa_from_pfn(int pfn) {
    return (char *)physical_mem + pfn * PGSIZE;
}

void* get_va_from_vfn(int vfn) {
    return (char *)0x0 + vfn * PGSIZE;
}

int get_pfn_from_pa(void* pa) {
    return ((char *)pa - (char *)physical_mem) / PGSIZE;
}

int get_vfn_from_va(void* va) {
    return ((char *)va - (char *)0x0) / PGSIZE;
}

int get_bit(char* bitmap, int index) {
    return ((bitmap[index / 8] >> (index % 8)) & 1);
}

void set_bit(char* bitmap, int index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

void clear_bit(char* bitmap, int index) {
    bitmap[index / 8] &= ~(1 << (index % 8));
}

int get_offset_bits() {
    return log(PGSIZE) / log(2);
}

int get_index_bits() {
    return log(PT_ENTRIES) / log(2);
}

void* get_next_avail_pa() {
    for (int i = 0; i < NUM_PHYSICAL_PAGES / 8; i++) {
        if (physical_bitmap[i] == 0xFF) {
            continue;
        }
        for (int j = 0; j < 8; j++) {
            if (!(physical_bitmap[i] & (1 << j))) {
                return get_pa_from_pfn(i * 8 + j);
            }
        }
    }
    return NULL;
}

int pt_is_empty(int pd_idx) {
    int start_bit = pd_idx * PT_ENTRIES;
    int end_bit = start_bit + PT_ENTRIES - 1;
    for (int i = start_bit / 8; i <= end_bit / 8; i++) {
        if (virtual_bitmap[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {

    //Allocate physical memory using mmap or malloc; this is the total size of
    //your memory you are simulating

    physical_mem = malloc(MEMSIZE);
    memset(physical_mem, 0, MEMSIZE);

    //HINT: Also calculate the number of physical and virtual pages and allocate
    //virtual and physical bitmaps and initialize them

    physical_bitmap = malloc(NUM_PHYSICAL_PAGES / 8);
    memset(physical_bitmap, 0, NUM_PHYSICAL_PAGES / 8);

    virtual_bitmap = malloc(NUM_VIRTUAL_PAGES / 8);
    memset(virtual_bitmap, 0, NUM_VIRTUAL_PAGES / 8);
    
    page_directory = (pde_t *)get_pa_from_pfn(0);
    set_bit(physical_bitmap, 0);
    set_bit(virtual_bitmap, 0);

    tlb_store = malloc(TLB_ENTRIES * sizeof(tlb_entry));
    memset(tlb_store, 0, TLB_ENTRIES * sizeof(tlb_entry));
}


/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int add_TLB(void *va, void *pa) {

    /*Part 2 HINT: Add a virtual to physical page translation to the TLB */

    pte_t vaddr = (pte_t) va;
    pte_t vpn = vaddr >> get_offset_bits();
    pte_t paddr = (pte_t) pa;
    pte_t ppn = paddr >> get_offset_bits();

    int index = vpn % TLB_ENTRIES;
    tlb_store[index].tag = vpn;
    tlb_store[index].value = ppn;
    return 0;
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t* check_TLB(void *va) {

    /* Part 2: TLB lookup code here */

    /*This function should return a pte_t pointer*/

    pte_t vaddr = (pte_t) va;
    pte_t vpn = vaddr >> get_offset_bits();
    pte_t offset = vaddr & ((1 << get_offset_bits()) - 1);

    int index = vpn % TLB_ENTRIES;
    pte_t tlb_vpn = tlb_store[index].tag;
    pte_t tlb_ppn = tlb_store[index].value;
    if (tlb_vpn == vpn && tlb_ppn != 0) {
        pte_t paddr = (tlb_ppn << get_offset_bits()) | offset;
        return (pte_t *)paddr;
    }
    return NULL;
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void print_TLB_missrate() {
    double miss_rate = 0;	

    /*Part 2 Code here to calculate and print the TLB miss rate*/

    miss_rate = (double)tlb_misses / (double)tlb_lookups;

    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}



/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va) {
    /* Part 1 HINT: Get the Page directory index (1st level) Then get the
    * 2nd-level-page table index using the virtual address.  Using the page
    * directory index and page table index get the physical address.
    *
    * Part 2 HINT: Check the TLB before performing the translation. If
    * translation exists, then you can return physical address from the TLB.
    */

    tlb_lookups++;
    pte_t* tlb_pa = check_TLB(va);
    if (tlb_pa != NULL) {
        return tlb_pa;
    }
    tlb_misses++;

    pte_t vaddr = (pte_t) va;
    pte_t pd_idx = (vaddr >> (get_offset_bits() + get_index_bits())) & ((1 << get_index_bits()) - 1);
    pte_t pt_idx = (vaddr >> get_offset_bits()) & ((1 << get_index_bits()) - 1);
    pte_t offset = vaddr & ((1 << get_offset_bits()) - 1);

    pde_t pt_addr = pgdir[pd_idx];
    if (pt_addr != 0) {
        pte_t* page_table = (pte_t *)pt_addr;
        pte_t ppn = page_table[pt_idx];
        if (ppn != 0) {
            pte_t paddr = (ppn << get_offset_bits()) | offset;
            return (pte_t *)paddr;
        }
    }

    //If translation not successful, then return NULL
    return NULL; 
}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int page_map(pde_t *pgdir, void *va, void *pa) {

    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */

    pte_t vaddr = (pte_t) va;
    pte_t pd_idx = (vaddr >> (get_offset_bits() + get_index_bits())) & ((1 << get_index_bits()) - 1);
    pte_t pt_idx = (vaddr >> get_offset_bits()) & ((1 << get_index_bits()) - 1);
    pte_t paddr = (pte_t) pa;
    pte_t ppn = paddr >> get_offset_bits();

    pde_t pt_addr = pgdir[pd_idx];
    if (pt_addr != 0) {
        pte_t* page_table = (pte_t *)pt_addr;
        if (page_table[pt_idx] == 0) {
            page_table[pt_idx] = ppn;
            add_TLB(va, pa);
            tlb_misses++;
            return 0;
        }
    }

    return -1;
}


/*Function that gets the next available page
*/
void *get_next_avail(int num_pages) {

    //Use virtual address bitmap to find the next free page

    int start_vfn = 0;
    while (start_vfn < NUM_VIRTUAL_PAGES) {
        int i;
        for (i = 0; i < num_pages; i++) {
            if (virtual_bitmap[(start_vfn + i) / 8] & (1 << ((start_vfn + i) % 8))) {
                start_vfn += i + 1;
                break;
            }
        }
        if (i == num_pages) {
            break;
        }
    }
    if (start_vfn >= NUM_VIRTUAL_PAGES) {
        return NULL;
    }
    return get_va_from_vfn(start_vfn);
}


/* Function responsible for allocating pages
and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes) {

    /* 
     * HINT: If the physical memory is not yet initialized, then allocate and initialize.
     */

   /* 
    * HINT: If the page directory is not initialized, then initialize the
    * page directory. Next, using get_next_avail(), check if there are free pages. If
    * free pages are available, set the bitmaps and map a new page. Note, you will 
    * have to mark which physical pages are used. 
    */

    pthread_mutex_lock(&mutex);
    if (physical_mem == NULL) {
        set_physical_mem();
    }
    int num_pages = (num_bytes + PGSIZE - 1) / PGSIZE;
    void* next_avail = get_next_avail(num_pages);
    if (next_avail != NULL) {
        int start_vfn = get_vfn_from_va(next_avail);
        int end_vfn = start_vfn + num_pages - 1;
        if (end_vfn >= len_pt * PT_ENTRIES) {
            int num_pt = (end_vfn + 1 - len_pt * PT_ENTRIES + PT_ENTRIES - 1) / PT_ENTRIES;
            for (int i = 0; i < num_pt; i++) {
                void* next_avail_pa = get_next_avail_pa();
                int pfn = get_pfn_from_pa(next_avail_pa);
                page_directory[len_pt++] = (pde_t)get_pa_from_pfn(pfn);
                set_bit(physical_bitmap, pfn);
            }
        }
        for (int i = 0; i < num_pages; i++) {
            void* next_avail_pa = get_next_avail_pa();
            int pfn = get_pfn_from_pa(next_avail_pa);
            int vfn = start_vfn + i;
            void* pa = get_pa_from_pfn(pfn);
            void* va = get_va_from_vfn(vfn);
            if (page_map(page_directory, va, pa) == 0) {
                set_bit(physical_bitmap, pfn);
                set_bit(virtual_bitmap, vfn);
            }
        }
        pthread_mutex_unlock(&mutex);
        return next_avail;
    }
    pthread_mutex_unlock(&mutex);
    return NULL;
}

/* Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     *
     * Part 2: Also, remove the translation from the TLB
     */

    pthread_mutex_lock(&mutex);
    int num_pages = (size + PGSIZE - 1) / PGSIZE;
    int start_vfn = get_vfn_from_va(va);
    for (int i = 0; i < num_pages; i++) {
        int vfn = start_vfn + i;
        if (!get_bit(virtual_bitmap, vfn)) {
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    for (int i = 0; i < num_pages; i++) {
        void* curr_va = (char *)va + i * PGSIZE;
        void* curr_pa = (void *)translate(page_directory, curr_va);
        int pfn = get_pfn_from_pa(curr_pa);
        int vfn = start_vfn + i;
        clear_bit(physical_bitmap, pfn);
        clear_bit(virtual_bitmap, vfn);

        pte_t vaddr = (pte_t) curr_va;
        pte_t vpn = vaddr >> get_offset_bits();
        pte_t pd_idx = (vaddr >> (get_offset_bits() + get_index_bits())) & ((1 << get_index_bits()) - 1);
        pte_t pt_idx = (vaddr >> get_offset_bits()) & ((1 << get_index_bits()) - 1);

        pde_t pt_addr = page_directory[pd_idx];
        if (pt_addr != 0) {
            pte_t* page_table = (pte_t *)pt_addr;
            page_table[pt_idx] = 0;
            if (pt_is_empty(pd_idx) == 1) {
                int pt_pfn = get_pfn_from_pa((void *)page_table);
                clear_bit(physical_bitmap, pt_pfn);
            }

            int index = vpn % TLB_ENTRIES;
            pte_t tlb_vpn = tlb_store[index].tag;
            pte_t tlb_ppn = tlb_store[index].value;
            if (tlb_vpn == vpn && tlb_ppn != 0) {
                tlb_store[index].tag = 0;
                tlb_store[index].value = 0;
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}


/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
int put_value(void *va, void *val, int size) {

    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */


    /*return -1 if put_value failed and 0 if put is successfull*/
    pthread_mutex_lock(&mutex);
    pte_t vaddr = (pte_t) va;
    pte_t offset = vaddr & ((1 << get_offset_bits()) - 1);
    int num_pages = (offset + size + PGSIZE - 1) / PGSIZE;
    int start_vfn = get_vfn_from_va(va);
    for (int i = 0; i < num_pages; i++) {
        int vfn = start_vfn + i;
        if (!get_bit(virtual_bitmap, vfn)) {
            pthread_mutex_unlock(&mutex);
            return -1;
        }
    }

    void* curr_va = va;
    void* curr_val = val;
    int curr_size = size;
    for (int i = 0; i < num_pages; i++) {
        int bytes_to_copy = (curr_size > PGSIZE - offset) ? (PGSIZE - offset) : curr_size;
        void* curr_pa = (void *)translate(page_directory, curr_va);
        memcpy(curr_pa, curr_val, bytes_to_copy);

        offset = 0;
        curr_va = (char *)curr_va + bytes_to_copy;
        curr_val = (char *)curr_val + bytes_to_copy;
        curr_size -= bytes_to_copy;
    }
    pthread_mutex_unlock(&mutex);
    return 0;
}


/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {

    /* HINT: put the values pointed to by "va" inside the physical memory at given
    * "val" address. Assume you can access "val" directly by derefencing them.
    */

    pthread_mutex_lock(&mutex);
    pte_t vaddr = (pte_t) va;
    pte_t offset = vaddr & ((1 << get_offset_bits()) - 1);
    int num_pages = (offset + size + PGSIZE - 1) / PGSIZE;

    void* curr_va = va;
    void* curr_val = val;
    int curr_size = size;
    for (int i = 0; i < num_pages; i++) {
        int bytes_to_copy = (curr_size > PGSIZE - offset) ? (PGSIZE - offset) : curr_size;
        void* curr_pa = (void *)translate(page_directory, curr_va);
        memcpy(curr_val, curr_pa, bytes_to_copy);

        offset = 0;
        curr_va = (char *)curr_va + bytes_to_copy;
        curr_val = (char *)curr_val + bytes_to_copy;
        curr_size -= bytes_to_copy;
    }
    pthread_mutex_unlock(&mutex);
}



/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));
                // printf("Values at the index: %d, %d, %d, %d, %d\n", 
                //     a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}
