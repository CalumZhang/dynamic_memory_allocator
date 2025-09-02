/**
 * @file mm.c
 * @brief A 64-bit struct-based segregated free list memory allocator
 *
 * This implementation of dymanic memory allocator is based on the data
 * structure of segregated free list (with an additioanl mini-block list that
 * stores minimum-sized blocks).
 *
 * For each block, it contains a header and a payload area.
 * 
 * For each mini block, it contains a header and a pointer to the next mini
 * block.
 *
 * The size of the block is packed in its header with the four least-significant
 * bits set to zero, as the result of 16-byte alignment. The least significant
 * bit of the header is set to indicate the block allocation status. The second
 * to least significant bit is set to indicate the allocation status of the 
 * previous block. The third to least significant bit is set to indicate the 
 * mini status of the previous block.
 *
 * The payload area is implementated using union and contains both the users'
 * data and two pointers pointing to the previous and next blocks, respectively.
 * Thus, each free list is a non-circular double-linked list that allows
 * traverse between adjacent free blocks.
 *
 * The segregated list is an array of explicit lists partitioned according to
 * different block sizes. There are in total 14 buckets in this segregated list.
 * The first bucket contains all blocks with sizes smaller or equal to 2^5, 
 * and the following buckets are classified according the powers of 2. This 
 * implementation uses the LIFO strategy to insert and remove any block in the 
 * current free list.
 *
 * The program also uses the mm_checkheap function to track the heap performance
 * and check for invariants.
 *
 * For further details, see the specific comments under each function below.
 *
 *
 *************************************************************************
 *************************************************************************
 *
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

// Optimal segregated list length
#define LENGTH 14

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printf(...) ((void)printf(__VA_ARGS__))
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, these should emit no code whatsoever,
 * not even from evaluation of argument expressions.  However,
 * argument expressions should still be syntax-checked and should
 * count as uses of any variables involved.  This used to use a
 * straightforward hack involving sizeof(), but that can sometimes
 * provoke warnings about misuse of sizeof().  I _hope_ that this
 * newer, less straightforward hack will be more robust.
 * Hat tip to Stack Overflow poster chqrlie (see
 * https://stackoverflow.com/questions/72647780).
 */
#define dbg_discard_expr_(...) ((void)((0) && printf(__VA_ARGS__)))
#define dbg_requires(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_assert(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_ensures(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_printf(...) dbg_discard_expr_(__VA_ARGS__)
#define dbg_printheap(...) ((void)((0) && print_heap(__VA_ARGS__)))
#endif

/* Basic constants */

typedef uint64_t word_t;

/** @brief Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);

/** @brief Double word size (bytes) */
static const size_t dsize = 2 * wsize;

/** @brief Minimum block size (bytes) */
static const size_t min_block_size = dsize;

/**
 * @brief Given no available free block, this is the size (in bytes) of the
 * block extended in the heap
 * (Must be divisible by dsize)
 */
static const size_t chunksize = (1 << 12);

/**
 * @brief Indicator of the block allocation status
 */
static const word_t alloc_mask = 0x1;

/**
 * @brief Indicator of the previous block allocation status
 */
static const word_t prev_alloc_mask = 0x2;

/**
 * @brief Indicator of the previous block mini status
 */
static const word_t prev_mini_mask = 0x4;

/**
 * @brief Indicator of the block size
 */
static const word_t size_mask = ~(word_t)0xF;

/** @brief Represents the header and payload of one block in the heap */
typedef struct block {
    word_t header;
    union {
        struct {
            struct block *prev;
            struct block *next;
        };
        char data[0];
    } payload;
} block_t;

/** @brief Represents structure of one mini block in the heap */
typedef struct mini_block {
    word_t header;
    struct mini_block *next;
} mini_block_t;

/* Global variables */

/** @brief Pointer to first block in the heap */
static block_t *heap_start = NULL;

/** @brief Desired segregated list as the partitioned form of explicit lists*/
static block_t *seg_list[LENGTH];

/** @brief List of blocks in minimum block size */
static mini_block_t *mini_list;

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details for the functions that you write yourself!               *
 *****************************************************************************
 */

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Returns the maximum of two integers.
 * @param[in] x
 * @param[in] y
 * @return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

/**
 * @brief Rounds `size` up to next multiple of n
 * @param[in] size
 * @param[in] n
 * @return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    return n * ((size + (n - 1)) / n);
}

/**
 * @brief Packs the 'size', 'alloc', 'prev_alloc' and 'prev_mini' of a block 
 * together into a word suitable for use as a packed value.
 *
 * Packed values are used for headers (for footers only when the block is free).
 *
 * The allocation status is packed into the lowest bit of the word.
 * The previous block allocation status is packed into the second to lowest bit 
 * of word.
 * The previous block mini status is packed into the third to lowest bit of
 * word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] alloc True if the block is allocated
 * @param[in] prev_alloc True if the previous block is allocated
 * @param[in] prev_mini True if the previous block is a mini block
 * @return The packed value
 */
static word_t pack_all(size_t size, bool alloc, bool prev_alloc, 
                   bool prev_mini) {
    word_t word = size;
    if (alloc) {
        word |= alloc_mask;
    }

    if (prev_alloc) {
        word |= prev_alloc_mask;
    }

    if (prev_mini) {
        word |= prev_mini_mask;
    }
    return word;
}

/**
 * @brief Extracts the size represented in a packed word.
 *
 * This function simply clears the lowest 4 bits of the word, as the heap
 * is 16-byte aligned.
 *
 * @param[in] word
 * @return The size of the block represented by the word
 */
static size_t extract_size(word_t word) {
    return (word & size_mask);
}


/**
 * @brief Extracts the size of a block from its header.
 * @param[in] block
 * @return The size of the block
 */
static size_t get_size(block_t *block) {
    return extract_size(block->header);
}

/**
 * @brief Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * @param[in] bp A pointer to a block's payload
 * @return The corresponding block
 */
static block_t *payload_to_header(void *bp) {
    return (block_t *)((char *)bp - offsetof(block_t, payload));
}


/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * @param[in] block
 * @return A pointer to the block's payload
 * @pre The block must be a valid block, not a boundary tag.
 */
static void *header_to_payload(block_t *block) {
    dbg_requires(get_size(block) != 0);
    return (void *)(&(block->payload));
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * @param[in] block
 * @return A pointer to the block's footer
 * @pre The block must be a valid block, not a boundary tag.
 */
static word_t *header_to_footer(block_t *block) {
    dbg_requires(get_size(block) != 0 &&
                 "Called header_to_footer on the epilogue block");
    char *payload = (char *)(&(block->payload));
    return (word_t *)(payload + get_size(block) - dsize);
}

/**
 * @brief Given a block footer, returns a pointer to the corresponding
 *        header.
 *
 * The header is found by subtracting the block size from
 * the footer and adding back wsize.
 *
 * If the prologue is given, then the footer is return as the block.
 *
 * @param[in] footer A pointer to the block's footer
 * @return A pointer to the start of the block
 */
static block_t *footer_to_header(word_t *footer) {
    size_t size = extract_size(*footer);
    dbg_requires(size != 0);
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * @brief Returns the payload size of a given block.
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and footer.
 *
 * @param[in] block
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    return asize - dsize;
}

/**
 * @brief Returns the allocation status of a given header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_alloc(word_t word) {
    return (bool)(word & alloc_mask);
}

/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}

/**
 * @brief Returns the previous block allocation status of a block, based on 
 * its header.
 * @param[in] block
 * @return The previous block allocation status of the block
 */

static bool get_prev_alloc(block_t *block) {
    dbg_requires(block != NULL);
    word_t header = block -> header;
    bool alloc = (bool) ((header & prev_alloc_mask) >> 1);
    return alloc;
}

/**
 * @brief Returns the previous block mini status of a block, based on its header.
 * @param[in] block
 * @return The previous block mini status of the block
 */

static bool get_prev_mini(block_t *block) {
    dbg_requires(block != NULL);
    word_t header = block -> header;
    bool alloc = (bool) ((header & prev_mini_mask) >> 2);
    return alloc;
}


/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block, bool prev_alloc, 
                           bool prev_mini) {
    dbg_requires(block != NULL);
    dbg_requires((char *)block == (char *)mem_heap_hi() - 7);
    block->header = pack_all(0, true, prev_alloc, prev_mini);
}

/**
 * @brief Finds the next consecutive block on the heap.
 *
 * This function accesses the next block in the "implicit list" of the heap
 * by adding the size of the block.
 *
 * @param[in] block A block in the heap
 * @return The next consecutive block on the heap
 * @pre The block is not the epilogue
 */
static block_t *find_next(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0 &&
                 "Called find_next on the last block in the heap");
    return (block_t *)((char *)block + get_size(block));
}

/**
 * @brief Writes a block starting at the given address.
 *
 * This function writes a header and writes a footer only when the block is 
 * neither allocated nor mini block, where the location of the footer is
 * computed in relation to the header.
 *
 * @param[out] block The location to begin writing the block header
 * @param[in] size The size of the new block
 * @param[in] alloc The allocation status of the new block
 * @param[in] prev_alloc The previous block allocation status of the new block
 * @param[in] prev_mini The previous block mini status of the new block
 */
static void write_pack(block_t *block, size_t size, bool alloc, 
                        bool prev_alloc, bool prev_mini) {
    dbg_requires(block != NULL);

    block->header = pack_all(size, alloc, prev_alloc, prev_mini);
    
    /* Write the footer only for free, non-mini blocks */
    if (!alloc && !(size == min_block_size)) {
        word_t *footerp = header_to_footer(block);
        *footerp = pack_all(size, alloc, prev_alloc, prev_mini);
    }
}

/**
 * @brief Determines if a block is a mini block
 *
 * @param[in] block A block in the heap
 * @return true if the block is a mini block, false othersie
 */

static bool is_mini_block(block_t *block) {
    size_t size = get_size(block);
    return (size == min_block_size);
}

/**
 * @brief Writes the previous block allocation status into the given block
 *
 * @param[out] block The location to update the header
 * @param[in] prev_alloc The previous block allocation status of the block
 */

static void write_prev_alloc(block_t *block, bool prev_alloc) {
    dbg_requires(block != NULL);

    if (prev_alloc) {
        block->header = block->header | prev_alloc_mask;
    }

    bool alloc = get_alloc(block);
    bool is_mini = is_mini_block(block);

    /* Write the footer only for free, non-mini blocks */
    if (!alloc && !is_mini) {
        word_t *footer = header_to_footer(block);
        if(prev_alloc) {
            *footer =  *footer | prev_alloc_mask;  
        }
    }
}

/**
 * @brief Finds the footer of the previous block on the heap.
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    return &(block->header) - 1;
}

/**
 * @brief Finds the previous consecutive block on the heap.
 *
 * This is the previous block in the "implicit list" of the heap.
 *
 * If the previous block is not a mini block, then the position of the previous
 * block is found by reading the previous block's footer to determine its size, 
 * then calculating the start of the previous block based on its size.
 * 
 * If the previous block is a mini block, then the position is found by the 
 * address of the current block subtracted by the minimum block size.
 *
 * @param[in] block A block in the heap
 * @return The previous consecutive block in the heap.
 * @pre The block is not the prologue
 */
static block_t *find_prev(block_t *block) {
    dbg_requires(block != NULL);

    bool prev_mini = get_prev_mini(block);

    /* Previous not a mini block */
    if (!prev_mini) {
        word_t *footerp = find_prev_footer(block);

        size_t not_head = extract_size(*footerp);
        if (!not_head) {
            return NULL;
        }

        return footer_to_header(footerp);

    } else {
        return (block_t*) ((char*) block - min_block_size);
    }
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/******** The remaining content below are helper and debug routines
 * ********/

/**
 * @brief Finds the specific free-list class in seg_list for the new block
 * given its size
 *
 * @pre The size is greater or equal to the miminum block size
 * @param[in] asize the size of the given block
 * @return The optimal class selected for the block
 */

static size_t find_class(size_t asize) {
    dbg_requires(asize >= min_block_size);

    if (asize < 32) {
        return (size_t) 0;
    } else if (asize < 64) {
        return (size_t) 1;
    } else if (asize < 128) {
        return (size_t) 2;
    } else if (asize < 256) {
        return (size_t) 3;
    } else if (asize < 512) {
        return (size_t) 4;
    } else if (asize < 1024) {
        return (size_t) 5;
    } else if (asize < 2048) {
        return (size_t) 6;
    } else if (asize < 3072) {
        return (size_t) 7;
    } else if (asize < 4096) {
        return (size_t) 8;
    } else if (asize < 6656) {
        return (size_t) 9;
    } else if (asize < 8192) {
        return (size_t) 10;
    } else if (asize < 16384) {
        return (size_t) 11;
    } else if (asize < 32768) {
        return (size_t) 12;
    } else {
        return (size_t) 13;
    }
}

/**
 * @brief Determines if the given block is the head block of one free list in
 * seg_list
 *
 * @param[in] block A block in the heap
 * @return If the block is a head block, then returns its corresponding class
 * index; otherwise, returns -1
 */

static int is_head(block_t *block) {

    size_t class = find_class(get_size(block));
    block_t *curr = seg_list[class];

    if (block == curr) {
        return (int)class;
    }

    else {
        return -1;
    }
}

/**
 * @brief Inserts the given new block pointer into the head of its corresponding
 * free list in seg_list
 *
 * @param[out] block The location to insert the new free block
 */

static void insert_free(block_t *block) {
    dbg_requires(block != NULL);

    /* For mini-block */
    if (is_mini_block(block)) {
        mini_block_t *curr_mini = (mini_block_t*) block;

        /* Given that the current mini list is not empty */
        if (mini_list != NULL) {
            curr_mini->next = mini_list;
            mini_list = curr_mini;
        }

        /* Given that the current mini list is empty */
        else {
            mini_list = curr_mini;
            mini_list->next = NULL;
        }

        return;
    }

    size_t class = find_class(get_size(block));
    block_t *curr = seg_list[class];
    seg_list[class] = block;

    /* Given that the current free list is not empty */
    if (curr != NULL) {
        curr->payload.prev = block;
        block->payload.next = curr;
    }

    /* Given that the current free list is empty */
    else {
        block->payload.next = NULL;
        block->payload.prev = NULL;
    }

    return;
}

/**
 * @brief Removes the given block pointer from its free list in seg_list
 *
 * @param[out] block The location of the block to be removed
 */

static void remove_free(block_t *block) {
    dbg_requires(block != NULL);

    /* For mini-block */
    if (is_mini_block(block)) {
        mini_block_t *mini_block = (mini_block_t*) block;

        /* Case when mini list only has one element */
        if (mini_list -> next == NULL) {
            mini_list = NULL;
        }

        /* Case when the block is the head of the mini list */
        else if (mini_block == mini_list) {
            mini_list = mini_list->next;
        }

        /* Case when the block is in the middle or tail of the mini list */
        else {
            mini_block_t* curr = mini_list;
            while (curr -> next != mini_block) {
                curr = curr->next;
            }
            
            curr->next = mini_block->next;
        }

        return;
    }

    block_t *prev = block->payload.prev;
    block_t *next = block->payload.next;

    int head_ind = is_head(block);
    bool head;
    if (head_ind == -1) {
        head = false;
    } else {
        head = true;
    }

    bool tail = (block->payload.next == NULL);

    /* Case when the block is the head */
    if (head) {
        size_t class = (size_t)head_ind;
        seg_list[class] = block->payload.next;
        return;
    }

    /* Case when the block is the tail */
    if (tail) {
        prev->payload.next = NULL;
        return;
    }

    /* Case when the block is in the middle of its free list */
    if (!head && !tail) {
        prev->payload.next = next;
        next->payload.prev = prev;
        return;
    }
}

/**
 * @brief Coalesces the given free block with its neighbor free blocks, if there
 * are any
 * @pre get_alloc(block) == false && block != NULL
 *
 * @param[in] block The location of the block to be coalesced
 * @return The location of the larger free block after coalescing
 */
static block_t *coalesce_block(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(!get_alloc(block));

    block_t *prev;
    block_t *next;
    size_t total_size;
    bool prev_alloc, next_alloc;

    /* Get the size of the current block */
    size_t current_size = get_size(block);

    /* Get the allocation status of adjancent blocks */
    prev_alloc = get_prev_alloc(block);
    if (!prev_alloc) {
        prev = find_prev(block);
    }

    next = find_next(block);
    next_alloc = get_alloc(next);
    size_t next_size = get_size(next);

    /* Case one: both prev and next are allocated */
    if (prev_alloc && next_alloc) {

        write_pack(next, next_size, true, false, is_mini_block(block));

        insert_free(block);
        return block;
    }

    /* Case two: prev is free and next is allocated */
    else if (!prev_alloc && next_alloc) {
        remove_free(prev);

        total_size = current_size + get_size(prev);
        bool prev_prev_alloc = get_prev_alloc(prev);
        bool prev_prev_mini = get_prev_mini(prev);

        write_pack(prev, total_size, false, prev_prev_alloc, prev_prev_mini);          
        write_pack(next, next_size, true, false, false);

        insert_free(prev);
        return prev;
    }

    /* Case three: prev is allocated and next if free */
    else if (prev_alloc && !next_alloc) {
        remove_free(next);

        total_size = current_size + next_size;
        bool prev_mini = get_prev_mini(block);

        write_pack(block, total_size, false, true, prev_mini);

        block_t *next_next = find_next(next);
        size_t next_next_size = get_size(next_next);
        bool next_next_alloc = get_alloc(next_next);

        write_pack(next_next, next_next_size, next_next_alloc, false, false);

        insert_free(block);
        return block;
    }

    /* Case four: both prev and next are free */
    else {
        remove_free(prev);
        remove_free(next);

        total_size = current_size + get_size(prev) + get_size(next);
        bool prev_prev_alloc = get_prev_alloc(prev);
        bool prev_prev_mini = get_prev_mini(prev);

        write_pack(prev, total_size, false, prev_prev_alloc, prev_prev_mini); 

        block_t *next_next = find_next(next);
        size_t next_next_size = get_size(next_next);
        bool next_next_alloc = get_alloc(next_next);

        write_pack(next_next, next_next_size, next_next_alloc, false, false);

        insert_free(prev);
        return prev;
    }
}


/**
 * @brief Extends the current heap to have an extra large free block
 *
 * @param[in] size The size of extension (at least size + wsize to maintain
 * alignment)
 * @return The location of the newly extended large free block, coalesced with
 * the previous free block if applicable
 */
static block_t *extend_heap(size_t size) {
    void *bp;

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk((intptr_t)size)) == (void *)-1) {
        return NULL;
    }

    // Initialize free block header/footer
    block_t *block = payload_to_header(bp);
    bool prev_alloc = get_prev_alloc(block);
    bool prev_mini = get_prev_mini(block);
    write_pack(block, size, false, prev_alloc, prev_mini);

    // Create new epilogue header
    block_t *block_next = find_next(block);
    write_epilogue(block_next, get_alloc(block), is_mini_block(block));

    // Coalesce in case the previous block was free
    block = coalesce_block(block);

    return block;
}

/**
 * @brief Splits the given allocated block if it is too large to store the data
 * of asize, resulting in an allocated block of asize and the remaining space
 * as a new free block
 *
 * @pre get_alloc(block) == true
 * @param[in] block The location of the large allocated block to be splitted
 * @param[in] asize The space that is actually needed within the block
 * @return The location of the newly splitted free block, or NULL if there isn't
 * any
 */

static block_t *split_block(block_t *block, size_t asize) {
    dbg_requires(get_alloc(block));

    block_t *block_next;
    block_t *next_next;
    size_t block_size = get_size(block);
    bool prev_alloc = get_prev_alloc(block);
    bool prev_mini = get_prev_mini(block);

    if ((block_size - asize) >= min_block_size) {
 
        write_pack(block, asize, true, prev_alloc, prev_mini);

        block_next = find_next(block);

        write_pack(block_next, block_size - asize, false, true, 
        (asize == min_block_size));

        next_next = find_next(block_next);

        write_prev_alloc(next_next, false);
        
        return block_next;
    }

    dbg_ensures(get_alloc(block));
    return NULL;
}


/**
 * @brief Finds the corresponding class of the given data in the segregated 
 * list. Then, searches the current and following classes with a better-fit 
 * approach to find a free block that is large enough to store the data of asize
 * while maximizing the memory utilization
 *
 * @param[in] asize The needed size
 * @return The location of the free block founded, or NULL if there isn't one
 */

static block_t *find_fit(size_t asize) {
    dbg_requires(asize > 0);

    /* For mini-block, use the first free block in mini list if there is one */
    if(asize == min_block_size && mini_list != NULL) {
        return (block_t*) mini_list;
    }

    size_t class = find_class(asize);

    for (size_t i = class; i < LENGTH; i++) {

        block_t *best = NULL;
        block_t *block = seg_list[i];

        /* Search for each class */
        while (block != NULL) {

            if (!(get_alloc(block)) && (asize <= get_size(block))) {

                if (best == NULL) {
                    best = block;
                }
                    
                else if (get_size(block) < get_size(best)) {
                    best = block;
                }
                    
                else {
                    return best;
                }
                
            } 

            block = block->payload.next;
        }

        /* Return if one is found after finishing searching for one class */
        if (best != NULL) {
            return best;
        }
    }

    return NULL;
}


/**
 * @brief
 * Checks if prologue and epilogue are allocated and have size zero
 */

static bool check_prologue_epilogue(void) {

    word_t *prologue = (word_t *)mem_heap_lo();
    block_t *epilogue = (block_t *)((char *)mem_heap_hi() - 7);

    /* Check for allocation status */
    bool pro_alloc_true = (extract_alloc(*prologue) == true);
    if (!pro_alloc_true) {
        dbg_printf("Prologue not allocated.\n");
        return false;
    }

    bool epi_alloc_true = (get_alloc(epilogue) == true);
    if (!epi_alloc_true) {
        dbg_printf("Epilogue not allocated.\n");
        return false;
    }

    /* Check for size */
    size_t pro_size = extract_size(*prologue);
    if (pro_size != 0) {
        dbg_printf("Incorrect prologue size.\n");
        return false;
    }

    size_t epi_size = get_size(epilogue);
    if (epi_size != 0) {
        dbg_printf("Incorrect epilogue size.\n");
        return false;
    }

    return true;
}

/**
 * @brief
 * Checks if the block payload is aligned with 16 bytes
 */

static bool check_alignment(block_t *block) {

    bool is_aligned = (((size_t)header_to_payload(block) % 16) == 0);

    if (!is_aligned) {
        dbg_printf("Misalignment at %p\n", (void *)block);
        return false;
    }

    return true;
}

/**
 * @brief
 * Checks if the block lies within the boundary
 */

static bool check_boundary(block_t *block) {

    if ((void *)block > mem_heap_hi()) {
        dbg_printf("Block out of upper bound %p\n", (void *)block);
        return false;
    }

    if ((void *)block < mem_heap_lo()) {
        dbg_printf("Block out of lower bound %p\n", (void *)block);
        return false;
    }

    return true;
}

/**
 * @brief
 * Checks if there are two consecutive free blocks (coalesce fails)
 */

static bool check_non_consecutive_free(block_t *block) {

    block_t *next = find_next(block);

    bool curr_alloc = get_alloc(block);
    bool next_alloc = get_alloc(next);

    if (!curr_alloc && !next_alloc) {
        dbg_printf("Two consecutive free blocks.\n");
        dbg_printf("First block is %p\n", (void *)block);
        dbg_printf("Second block is %p\n", (void *)next);
        return false;
    }

    return true;
}

/**
 * @brief
 * Checks if the header and footer of the block match
 */

static bool check_header_footer_match(block_t *block) {

    bool curr_alloc = get_alloc(block);
    word_t header = block->header;
    word_t footer = *(header_to_footer(block));
    block_t *next = find_next(block);
    bool is_mini = get_prev_mini(next);

    if (!curr_alloc && !is_mini && (header != footer)) {
        dbg_printf("Header and footer do not match.\n");
        dbg_printf("The block is %p\n", (void *)block);
        return false;
    }

    return true;
}

/**
 * @brief
 * Checks if the block size is valid
 */

static bool check_block_size(block_t *block) {

    size_t size = get_size(block);

    if ((size < min_block_size) || (size % 16 != 0)) {
        dbg_printf("Invalid block size at %p\n", (void *)block);
        return false;
    }

    return true;
}

/**
 * @brief
 * Checks if the heap is valid
 */

static bool general_heap_checker(void) {
    dbg_requires(heap_start != NULL);

    if (!check_prologue_epilogue()) {
        return false;
    }

    block_t *curr = heap_start;
    block_t *epilogue = (block_t *)((char *)mem_heap_hi() - 7);

    while (curr != epilogue) {

        if (!check_alignment(curr)) {
            return false;
        }

        if (!check_boundary(curr)) {
            return false;
        }

        if (!check_block_size(curr)) {
            return false;
        }

        if (!check_header_footer_match(curr)) {
            return false;
        }

        if (!check_non_consecutive_free(curr)) {
            return false;
        }

        curr = find_next(curr);
    }

    return true;
}

/**
 * @brief
 * Checks if the segregated list is valid
 */

static bool check_list(void) {

    for (size_t i = 0; i < LENGTH; i++) {
        block_t *curr = seg_list[i];

        while (curr != NULL) {

            /* Checks if the free list pointer is between mem_heap_lo() and
            mem_heap_high() */
            if ((void *)curr > mem_heap_hi()) {
                dbg_printf("Block out of upper bound %p\n", (void *)curr);
                return false;
            }

            if ((void *)curr < mem_heap_lo()) {
                dbg_printf("Block out of lower bound %p\n", (void *)curr);
                return false;
            }

            /* Checks if the next/previous pointers are consistent */
            block_t *next = curr->payload.next;
            if (next != NULL) {
                bool is_prev = (next->payload.prev == curr);
                if (!is_prev) {
                    dbg_printf("Next/previous pointers are not consistent.\n");
                    dbg_printf("The block is %p\n", (void *)curr);
                    dbg_printf("The next block is %p\n", (void *)next);
                    return false;
                }
            }

            /* Checks if the block falls within the desired bucket size range */
            size_t class = find_class(get_size(curr));
            if (class != i) {
                dbg_printf("The block is not in the desired list bucket.\n");
                dbg_printf("The block is %p\n", (void *)curr);
                return false;
            }

            curr = curr->payload.next;
        }
    }


    return true;
}

/**
 * @brief Overall heap cheacker than tracks heap performance and checks for
 * invariants
 *
 * @param[in] line The line where the assertion failure raises, given the
 * function returns false
 * @return true if the heap check passes, and false otherwise
 */
bool mm_checkheap(int line) {

    if (!general_heap_checker()) {
        return false;
    }

    if (!check_list()) {
        return false;
    }

    return true;
}



/**
 * @brief Initializes the heap, segregated free list, and mini list
 * @return true if the initialization succeeds, and false otherwise
 */

bool mm_init(void) {

    // Create the initial empty heap
    word_t *start = (word_t *)(mem_sbrk(2 * wsize));

    if (start == (void *)-1) {
        return false;
    }

    /* Initialize segregated free list */
    for (size_t i = 0; i < LENGTH; i++) {
        seg_list[i] = NULL;
    }

    /* Initialize the mini-block list */
    mini_list = NULL;

    start[0] = pack_all(0, true, false, false); // Heap prologue (block footer)
    start[1] = pack_all(0, true, true, false); // Heap epilogue (block header)

    // Heap starts with first "block header", currently the epilogue
    heap_start = (block_t *)&(start[1]);

    // Extend the empty heap with a free block of chunksize bytes
    if (extend_heap(chunksize) == NULL) {
        return false;
    }

    return true;
}


/**
 * @brief The fundamental dynamic memory allocator that allocates size bytes
 * of date on the heap
 *
 * @param[in] size The number of bytes to store on the heap
 * @return The location of the payload of the allocated block, otherwise NULL
 * if the allocation fails
 */

void *malloc(size_t size) {
    dbg_requires(mm_checkheap(__LINE__));

    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;
    void *bp = NULL;

    // Initialize heap if it isn't initialized
    if (heap_start == NULL) {
        if (!(mm_init())) {
            dbg_printf("Problem initializing heap. Likely due to sbrk");
            return NULL;
        }
    }

    // Ignore spurious request
    if (size == 0) {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment requirements
    asize = round_up(size + wsize, dsize);

    // Search the free list for a fit
    block = find_fit(asize);

    // If no fit is found, request more memory, and then and place the block
    if (block == NULL) {
        // Always request at least chunksize
        extendsize = max(asize, chunksize);
        block = extend_heap(extendsize);
        // extend_heap returns an error
        if (block == NULL) {
            return bp;
        }
    }

    dbg_assert(!get_alloc(block));

    // Mark block as allocated
    remove_free(block);
    
    size_t block_size = get_size(block);
    bool prev_alloc = get_prev_alloc(block);
    bool prev_mini = get_prev_mini(block);

    write_pack(block, block_size, true, prev_alloc, prev_mini);

    block_t *next = find_next(block);
    write_prev_alloc(next, true);

    block_t *temp = split_block(block, asize);
    if (temp != NULL) {
        coalesce_block(temp);
    }

    bp = header_to_payload(block);

    dbg_ensures(mm_checkheap(__LINE__));
    return bp;
}

/**
 * @brief Frees the block with the bp payload address, and coalesces this block
 * with its neighbor free blocks if possible
 *
 * @param[in] bp The payload address of the block to be freed
 */
void free(void *bp) {
    dbg_requires(mm_checkheap(__LINE__));

    if (bp == NULL) {
        return;
    }

    block_t *block = payload_to_header(bp);

    // The block should be marked as allocated
    dbg_assert(get_alloc(block));

    size_t block_size = get_size(block);
    bool prev_alloc = get_prev_alloc(block);
    bool prev_mini = get_prev_mini(block);

    write_pack(block, block_size, false, prev_alloc, prev_mini);

    block_t *next = find_next(block);
    write_prev_alloc(next, false);

    // Try to coalesce the block with its neighbors
    coalesce_block(block);

    dbg_ensures(mm_checkheap(__LINE__));
}

/**
 * @brief Changes the size of a block that is already allocated and reallocates
 * it with at least size bytes of data
 *
 * @param[in] ptr The payload address of the block to be reallocated
 * @param[in] size The number of bytes to be reallocated
 * @return The location of the block that is newly reallocated
 */
void *realloc(void *ptr, size_t size) {
    dbg_requires(mm_checkheap(__LINE__));

    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL) {
        return NULL;
    }

    // Copy the old data
    copysize = get_size(block) - wsize; // gets size of old payload
    if (size < copysize) {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/**
 * @brief Array version of malloc with each element of size bytes, while 
 * initializing all memory bytes to zero
 *
 * @param[in] elements The number of elements to be allocated
 * @param[in] size The size of each element
 * @return The location of the payload of the allocated block, otherwise NULL
 * if the allocation fails
 */
void *calloc(size_t elements, size_t size) {
    dbg_requires(mm_checkheap(__LINE__));

    void *bp;
    size_t asize = elements * size;

    if (elements == 0) {
        return NULL;
    }
    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/*
 *****************************************************************************
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a c5 7c fc 80 6e 57 0a               *
 *                                                                           *
 *****************************************************************************
 */
