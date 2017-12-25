
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "memlib.h"
#include "mm_thread.h"

/*
 * 
setenv TOPDIR /h/u9/g6/00/changkao/csc469/a2/starter_code/

/u/csc469h/fall/pub/a2-f2017/bin/check_timing.sh /h/u9/g6/00/changkao/csc469/a2/starter_code/allocators/a2alloc/a2alloc.c

*/

/******************************** Macros ***************************************/

// Variable Macros
// EF = emptiness fraction
#define EF 0.25
// K_VALUE 2
#define K_VALUE 7

#define MAXBIN 3  // Fullness groups (75-100%, 50-74%, 0-49%)


//  Each superblock size is the minimum requirement of 8192 (assuming PAGESIZE is 4196)
#define SBLKSZ PAGESIZE*2 


// Size classes { 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096}
// Base is 2, lowest class 2^3 = 8, highest class 2^12 = 4096
#define SZCLASS 10
#define BASE 2
#define MAXPOWER 12 
#define MINPOWER 3

// Other defined sums
#define CHECKSUM 123456789
#define NPAGE 16
#define NLARGEBLOCKS 250

// Macros functions
#define NHEAP(x) ((x % NPROCESSORS) + 1)
#define PWR_TO_IDX(x) ((x - MINPOWER))
#define PWR_TO_SZ(x) ((uint32_t) pow(BASE,x))
#define CHECK_BIT(var,pos) (((var)>>(pos)) & 1)
#define MAX_BLK_PER_SBLK(x) ((SBLKSZ - sizeof(superblock)) / (PWR_TO_SZ(x)))
/**************************************************************************/

/* Data Structures */

typedef struct superblock superblock;
typedef struct heap heap;
typedef struct largeblock largeblock;

struct superblock {
    heap * owner;  
    uint8_t power;
    uint32_t checksum; // checksum used to verify superblock
    uint32_t used; // amount used
    uint64_t bitmap[NPAGE];  
    superblock * next; 
    pthread_mutex_t lock;
};

struct heap {
    uint8_t id; // id of heap, 0 is global                  
    uint32_t used;                    
    uint32_t allocated;                  
    superblock * superblocks[MAXBIN][SZCLASS];
    pthread_mutex_t lock;    
};

struct largeblock {
    void * largeblocks[NLARGEBLOCKS]; //array of void * we store from mmap
    int size[NLARGEBLOCKS]; // associated size of each void * 
    largeblock * next;
};

/* Global Variables */

static int NPROCESSORS; // Number of processors
static int PAGESIZE; // Pagesize as defined by OS

static heap * HEAPS; // Global pointer to our heap
static largeblock * LARGEBLOCKS = NULL; // Global pointer to our largeblock list for storage of mallocs > 4196

static pthread_mutex_t MEMLOCK = PTHREAD_MUTEX_INITIALIZER; // Global locks for sbrk calls, as well as restructuring order of list
static pthread_mutex_t LARGEBLOCKLOCK = PTHREAD_MUTEX_INITIALIZER; // Only used for large block lists

/* 
 * 1: global mem_sbrk lock
 * 2: global heap lock
 * 3: global large block lock
 */
void lock_global(int i) { 
    if (i == 1) {
        pthread_mutex_lock(&MEMLOCK);
    } else if (i == 2) {
        pthread_mutex_lock(&((&HEAPS[0])->lock));
    } if (i == 3) {
        pthread_mutex_lock(&LARGEBLOCKLOCK);
    }
}

void unlock_global(int i) { 
    if (i == 1) {
        pthread_mutex_unlock(&MEMLOCK);
    } else if (i == 2) {
        pthread_mutex_unlock(&((&HEAPS[0])->lock));
    } if (i == 3) {
        pthread_mutex_unlock(&LARGEBLOCKLOCK);
    }
}

// lock/unlock functions

void lock_heap(heap * h) {pthread_mutex_lock(&(h->lock));}
void unlock_heap(heap * h) {pthread_mutex_unlock(&(h->lock));}
void lock_superblock(superblock * sblk) {pthread_mutex_lock(&(sblk->lock));}
void unlock_superblock(superblock * sblk) {pthread_mutex_unlock(&(sblk->lock));}
int trylock_superblock(superblock * sblk) {return pthread_mutex_trylock(&(sblk->lock));}


// Important macros for clearing and setting bits

uint64_t SET_BIT(uint64_t number, int pos) { return number |= 1ULL << pos;}
uint64_t CLEAR_BIT(uint64_t number, int pos) { return number &= ~(1ULL << pos);}



/**
 * Get the nearest power associated with the size, since we are working in powers of 2
 */
uint8_t get_power(size_t sz) {
    
    uint8_t power;
    // Min power is 3 -> 2^3 = 8
    // Max power is 12 -> 2^12 == 4196
    for (power = MINPOWER; power <= MAXPOWER; power++) {
        int sz_class = (int) pow(BASE,power);
        if (sz <= sz_class) {
            return power;
        }
    }
    return MINPOWER;   
}



/**
 * Allocate a superblock with the smallest size class
 */
superblock * allocate_new_superblock(size_t size) {

        // ask for memory for new superblock
	superblock * newblock = (superblock *) mem_sbrk(SBLKSZ);
	
        
        // Init the new superblock
	pthread_mutex_init(&(newblock->lock), NULL);
        uint8_t power = get_power(size);
	newblock->power = power;
        newblock->used = 0;
        newblock->checksum = CHECKSUM;
	newblock->next = NULL;
        // for each "page" in our bitmap, we set it to 0
        // we can mark a maximum of blocks = (64 bits * NPAGE) in our bitmap
	for (int i = 0; i < NPAGE; i++) {
		newblock->bitmap[i] = 0ULL;
	}

	return newblock;
}

/*
 *  Allocates a new block given a superblock, sets the bit in the superblock bitmap as in use
 */

void * allocate_new_block(superblock * sblk) {

        // finds the maximum amount blocks of its size class that can fit in the superblock 
	uint32_t max_blks = (uint32_t) MAX_BLK_PER_SBLK(sblk->power);
        // we have this as a bound since there can not be more than max_blks items in the superblock
        
        // nbit is the individual bit
        // page and idx are used as 64int means 64 bits which is the largest amounts of bits stored, thus
        // we have page amount of 64 ints, which means we can mark page*64 blocks in our bitmap
        // idx is the index of each page
        int nbit = 0;
	int page = 0;
	int idx = 0;
        uint64_t currentmap = sblk->bitmap[page];
        while (nbit < max_blks) {
            if (idx == 64) {
                idx = 0;
                page ++;
                currentmap = sblk->bitmap[page];
            }
            // check if bit is not set in the superblock's bitmap
            // and allocate the block at that address
            if (!(CHECK_BIT(currentmap, idx))) {
                
                // set the bit as used
                sblk->bitmap[page] = SET_BIT(currentmap, idx);
                void * blk;
                
                // skip the metadata of the superblock, and return the starting address of block
                blk = ((char *) sblk + sizeof(superblock)) + (nbit*(PWR_TO_SZ(sblk->power)));
                return blk;
            }
            nbit ++;
            idx ++;
            
        }
    
        return NULL;
}


/*
 *  Transfers the smallest used superblock in the target heap to the global heap
 */
int transfer_min_superblock(heap * target_heap) {
    
    heap * global_heap = &HEAPS[0];
    superblock * transfer_superblock = NULL;
    superblock * prev_transfer = NULL;      
    superblock * current;
    superblock * prev;
    int min = 1;
    int min_size_class = 0;
    int usage;
    int bin_idx;
    int size_idx;

    // search from bin with lowest usage first
    for (bin_idx = MAXBIN-1; bin_idx >= 0; bin_idx--){
        
        //for each size class
        for (size_idx = 0; size_idx < SZCLASS; size_idx++) {
        
            current = target_heap->superblocks[bin_idx][size_idx];
            prev = NULL;
            
            while (current) {
                
                usage = (float) current->used / (float) SBLKSZ;
                
                // we keep track of lowest used superblock
                if (usage < min) {
                    min = usage;
                    min_size_class = size_idx;
                    transfer_superblock = current;
                    prev_transfer = prev;
                }
                prev = current;
                current = current->next;
            }
        }
        // if there is a lowest used superblock given the bin, we
        // do not search other bins since we know they have higher usage
        if (transfer_superblock) {
            
            // do the transfer
            lock_superblock(transfer_superblock);
            
            target_heap->allocated -= SBLKSZ;
            target_heap->used -= transfer_superblock->used;
            global_heap->allocated += SBLKSZ;                
            global_heap->used += transfer_superblock->used;
            transfer_superblock->owner = global_heap;
            
            lock_global(1);
            
            // reorganize heap structure in target heap, and add superblock to global heap
            
            if (prev_transfer) {
                prev_transfer->next = transfer_superblock->next;
                transfer_superblock->next = global_heap->superblocks[bin_idx][min_size_class];
                global_heap->superblocks[bin_idx][min_size_class] = transfer_superblock;
            }
            else {
                target_heap->superblocks[bin_idx][min_size_class] = transfer_superblock->next;
                transfer_superblock->next = global_heap->superblocks[bin_idx][min_size_class];
                global_heap->superblocks[bin_idx][min_size_class] = transfer_superblock;      
            }
            unlock_global(1);
            unlock_superblock(transfer_superblock);
            return 0;
        }
    }
    return 1;
}

/*
 *  determine which bin the superblock should go to
 */

int determine_bin(superblock * sblk) {

    float p = (float)sblk->used / (float)SBLKSZ;
    if (p >= 0.75) {
        return 0;
    }
    else if (p >= 0.5) {
        return 1;
    }
    else {
        return 2;
    }
            
}


/*
 *  Determine the total shift from the superblock given a block ptr
 *  We use this to find out which superblock the block belongs to
 */

int determine_shft(void * ptr) {

    
    char * current_pos = ptr;
    int shfted_pos = 0;
    
    // Go back at max SBLKSZ, since the block is always contained in the superblock
    // We increment by the smallest class size (8)
    while (shfted_pos <= SBLKSZ) {
        if (((superblock *) current_pos)->checksum == CHECKSUM) {
            return shfted_pos;
        }
        current_pos -= 8;
        shfted_pos +=8;
    }
    return -1;
}

        
/*  We try to allocate the block for threads starting in the lowest bin
 *  If the current superblock is in use, we move onto another
 *  If all superblocks are in use, we will try to find one from the global heap
 *  or we will create a superblock
 */
void * try_allocate_block(heap * target_heap, size_t size) {
        
    
    uint8_t power = get_power(size);
    uint8_t size_class = PWR_TO_IDX(power);
    int max_blocks = MAX_BLK_PER_SBLK(power);
    int used_blocks;    
    int new_bin;
    int usage;
    superblock * current;
    superblock * prev;
    
    // search from highest used bin
    for (int nbin = 0; nbin < MAXBIN; nbin++) {
        
        current = target_heap->superblocks[nbin][size_class];
        prev = NULL;
        
        while (current) {
            
            used_blocks = current->used / (PWR_TO_SZ(power));
            if (used_blocks < max_blocks) {
            
                if (trylock_superblock(current) == 0) {
                
                    lock_global(1);
                    
                    if (prev) {
                        
                        prev->next = current->next;
                        current->next = NULL;
                    }
                    
                    else {
                        target_heap->superblocks[nbin][size_class] = current->next;
                    }

                    usage = PWR_TO_SZ(current->power);
                    target_heap->used += usage;
                    
                    current->used += usage;                
                    new_bin = determine_bin(current);
                    current->next = target_heap->superblocks[new_bin][size_class];                    
                    target_heap->superblocks[new_bin][size_class] = current;
                    
                    void * block = allocate_new_block(current);             
                    unlock_global(1);
                    unlock_superblock(current);
                    return block;
                }    
            }
        prev = current;
        current = current->next;
        }
    }
    return NULL;
}

void * try_allocate_global_block(heap * requesting_heap, size_t size) {
        
    heap * target_heap = &HEAPS[0];
    uint8_t power = get_power(size);
    uint8_t size_class = PWR_TO_IDX(power);
    int max_blocks = MAX_BLK_PER_SBLK(power);    
    int used_blocks;
    int new_bin;
    int usage;
    superblock * current;
    superblock * prev;

    
    for (int nbin = 0; nbin < MAXBIN; nbin++) {
        
        current = target_heap->superblocks[nbin][size_class];
        prev = NULL;
        
        // search through all the blocks
        while (current) {
            used_blocks = current->used / (PWR_TO_SZ(power));
            
            if (used_blocks < max_blocks) {
                
                // we lock this superblock
                lock_superblock(current);
                
                // lock our global lock since we are changing the list structure
                lock_global(1);

                // reorganize superblocks
                if (prev) {
                    prev->next = current->next;
                    current->next = NULL;
                }
                else {
                    target_heap->superblocks[nbin][size_class] = current->next;
                }
                
                // update heap meta data/ superblock metadata 
                
                usage = PWR_TO_SZ(current->power);
                target_heap->used -= current->used;
                target_heap->allocated -= SBLKSZ;
                
                requesting_heap->used += current->used;
                requesting_heap->used += SBLKSZ;
                
                // determine if the superblock needs to be moved to a higher
                // usage bin
                
                current->used += usage;
                current->owner = requesting_heap;            
                new_bin = determine_bin(current);
                current->next = requesting_heap->superblocks[new_bin][size_class];                    
                requesting_heap->superblocks[new_bin][size_class] = current;
                
                // call to allocate the new block
                void * block = allocate_new_block(current);   
                
                unlock_global(1);
                unlock_superblock(current);
                return block;
                 
            }
            prev = current;
            current = current->next;
        }
    }
    return NULL;
}


/* FUNCTION TO HANDLE LARGE BLOCKS
 * NO PERFORMANCE, Implemented most naively 
 */

// Init the large block array if called
int init_largeblocks() {

    lock_global(1);
    LARGEBLOCKS = (largeblock *) mem_sbrk(PAGESIZE);
    for (int i = 0; i < NLARGEBLOCKS; i ++ ){
        LARGEBLOCKS->largeblocks[i] = NULL;
    }
    LARGEBLOCKS->next = NULL;
    unlock_global(1);
    return 0;
    
}

// malloc the large block using mmap as specified on Piazza
void * malloc_largeblocks(size_t size) {
    
    largeblock * current = LARGEBLOCKS;
    largeblock * prev = NULL;
    // search through each large block struct to find empty slot in void * list
    while (current) {
        
        // we store an array of void * that we obtain from mmap
        // as well as the size of the requested mmap in a corresponding array in the
        // large block struct
        for (int i = 0; i < NLARGEBLOCKS; i ++ ){
            
            // if there is an open slot
            // add the void * to the list of void * for bookkeeping
            // as well as the size of the requested malloc
            if (!(current->largeblocks[i])) {
                current->size[i] = size;
                current->largeblocks[i] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_FILE |MAP_PRIVATE, -1,0);;
                return current->largeblocks[i];
            }
        }
        prev = current;
        current = current->next;
    }
    
    // if we cannot find a open slot in the large block struct list
    // we make a new struct, and use the first index [0] of the list
    // we then link the struct together
    lock_global(1);
    prev->next = (largeblock *) mem_sbrk(PAGESIZE);
    unlock_global(1);
    current = prev->next;
    current->largeblocks[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_FILE |MAP_PRIVATE, -1,0);;
    for (int i = 1; i < NLARGEBLOCKS; i ++ ){
        current->largeblocks[i] = NULL;
    }
    return current->largeblocks[0];
}


// Used to free large blocks
int free_largeblocks(void * ptr) {
    
    int free_size;
    void * free_addr;
    largeblock * current = LARGEBLOCKS;
    
    // search through each large block struct
    while (current) {
        
        // in each struct, find if the given ptr matches the stored void * ptrs
        // if it does, munmap it and remove the void * ptr from list
        for (int i = 0; i < NLARGEBLOCKS; i ++ ){
            
            if ((current->largeblocks[i]) == ptr) {
                free_size = current->size[i];
                free_addr = current->largeblocks[i];
                munmap(free_addr, free_size);
                
                current->size[i] = 0;
                current->largeblocks[i] = NULL;
                return 1;
            }
        }
        current = current->next;
    }
    return -1;
}

        
/**************************************************************/



/******************** MAIN FUNCTIONS **************************/

void *mm_malloc(size_t size) {

    void * candidate;
    
    if (size > SBLKSZ/2) {
        
        // to handle large block mallocs
        // obtain large block lock
        lock_global(3);
        
        // init if the large block list is NULL
        if (!LARGEBLOCKS) {
            init_largeblocks();
        }
        candidate = malloc_largeblocks(size);
        unlock_global(3);
        return candidate;

    }

    // get the heap ID from tid
    int heapid = NHEAP(getTID()); 
    heap * current_heap = &HEAPS[heapid];

    // lock the heap
    lock_heap(current_heap);
    
    // try to allocate block from highest usage superblocks
    candidate = try_allocate_block(current_heap, size);
    
    // if the superblocks are locked, we look at the global heap
    
    if (!candidate) {
        
        // search through global heap
        lock_global(2);
        candidate = try_allocate_global_block(current_heap, size);
        unlock_global(2);
    
	if (!candidate) {
            
            
            // create a new superblock for use 
            // update its meta data
            // reorganize heap and add it to list structure
            
            superblock * new_superblock = allocate_new_superblock(size);
            lock_superblock(new_superblock);
            //lock_global(1);
            int size_class = PWR_TO_IDX(new_superblock->power);
            int usage = PWR_TO_SZ(new_superblock->power);
            
            new_superblock->used += usage;
            new_superblock->owner = current_heap;
            current_heap->used += usage;  
            current_heap->allocated += SBLKSZ;
            int bin = determine_bin(new_superblock);
            lock_global(1);
            
            new_superblock->next = current_heap->superblocks[bin][size_class];                    
            current_heap->superblocks[bin][size_class] = new_superblock;
            
            candidate = allocate_new_block(new_superblock);
            
            unlock_global(1);
            unlock_superblock(new_superblock);
 
        }
    }
    unlock_heap(current_heap);
    return candidate;
    
}
            

void mm_free(void * ptr){
    
    // if ptr is null, return
    if (ptr == NULL) {
        printf("Null pointer");
        return; 
    }
    
    // if ptr is not within our memory range from mem_sbrk
    // we will let our large block function handle this
    if ((char *) ptr < dseg_lo || (char *) ptr > dseg_hi) {
        lock_global(3);
        free_largeblocks(ptr);
        unlock_global(3);
        return;
    }

    // obtain the shifted position from the ptr, to the nearest
    // superblock
    int shifted_pos = determine_shft(ptr);
    if (shifted_pos < 0) {
        printf("Error couldnt find memory blk");
        return;
    }
    // set the target superblock from the shifted position
    // obtain the heap through the target superblock
    superblock * target_superblock = (superblock *) ((char *) ptr - shifted_pos);    
    heap * target_heap = target_superblock->owner;
    
    // lock target heap and target superblock
    lock_heap(target_heap);
    lock_superblock(target_superblock);   
    
    int size_class = PWR_TO_IDX(target_superblock->power);
    int size = PWR_TO_SZ(target_superblock->power);
    int nbit = (shifted_pos - sizeof(superblock)) / size;
    
    // calculating bit to set
    int idx = nbit % 64;
    int page = nbit / 64;
    
    // unset the bit since we are "freeing" this memory for use
    target_superblock->bitmap[page] = CLEAR_BIT(target_superblock->bitmap[page], idx);
    target_heap->used -= size;
    
    // calculate new bin
    int old_bin = determine_bin(target_superblock);
    target_superblock->used -= size;
    int new_bin = determine_bin(target_superblock);

    // reorganize the heap by placing superblock into correct bin after free
    if (old_bin != new_bin) {
        
        superblock * prev = NULL;
        superblock * current = target_heap->superblocks[old_bin][size_class]; 
        
        while (current) {
        
            if (target_superblock == current) {
                
                // lock since we are changing the structure of the list
                lock_global(1);
                
                if (prev) {
                    prev->next = current->next;
                    current->next = NULL;
                }
                else {
                    
                    target_heap->superblocks[old_bin][size_class] = current->next;
                    
                }
                
                target_superblock->next = target_heap->superblocks[new_bin][size_class];
                target_heap->superblocks[new_bin][size_class] = target_superblock;
                unlock_global(1);
                break;
            }
            
            prev = current;
            current = current->next;
            
        }
    }
    
    unlock_superblock(target_superblock);
    
    // if the target heap is 0, we will unlock the heap and exit since it is global heap
    if (target_heap->id == 0) { 

        unlock_heap(target_heap);
        return;
    }

    // emptiness fraction from Hoard
    if (target_heap->used < target_heap->allocated - K_VALUE * SBLKSZ && target_heap->used < (1 - EF) * target_heap->allocated) {
        
        lock_global(2);
        // transfer minimum superblock
        transfer_min_superblock(target_heap);
        unlock_global(2);

        
    }
    unlock_heap(target_heap);
    return;
}


int mm_init(void) {
    
        
        // call the init function
        if (mem_init() == -1) {
            printf("Failed to mem_init\n");
            return -1;
        }
        
        
        // obtain the number of processors
        // and page size and store as global variables
	NPROCESSORS = getNumProcessors();
	PAGESIZE = mem_pagesize();
	HEAPS = (heap *) mem_sbrk(PAGESIZE);

        
	if (!HEAPS) {
                printf("Failed to sbrk\n");
		return -1;
	}

	// init the heap, heap[0] is the global heap
	// Have a maximum of nprocessors heaps
	for (int i = 0; i <= NPROCESSORS; i++) {
            
		heap * h = &HEAPS[i];
		pthread_mutex_init(&(h->lock), NULL);
		h->id = i;
		h->used = 0;
		h->allocated = 0;
                memset(h->superblocks, 0, sizeof(superblock *) * MAXBIN * SZCLASS);

	}
	return 0;
}


