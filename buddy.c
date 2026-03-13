#include "buddy.h"
#define NULL ((void *)0)

#define PAGE_SIZE (4 * 1024)
#define MAX_RANK 16
#define MAX_PAGES (128 * 1024 / 4) // Maximum pages we might need to track

// Free list node
typedef struct free_node {
    struct free_node *next;
} free_node_t;

// Global variables
static void *base_ptr = NULL;
static int total_pages = 0;
static free_node_t *free_lists[MAX_RANK + 1]; // free_lists[1] to free_lists[16]
static char page_ranks[MAX_PAGES]; // Track rank for each page (0 = free, 1-16 = allocated rank)

// Helper: Get page index from pointer
static inline int get_page_index(void *p) {
    if (p < base_ptr) return -1;
    long offset = (char *)p - (char *)base_ptr;
    if (offset < 0 || offset % PAGE_SIZE != 0) return -1;
    int idx = offset / PAGE_SIZE;
    if (idx >= total_pages) return -1;
    return idx;
}

// Helper: Get pointer from page index
static inline void *get_page_ptr(int idx) {
    return (char *)base_ptr + idx * PAGE_SIZE;
}

// Helper: Calculate buddy index
static inline int get_buddy_index(int idx, int rank) {
    int block_size = 1 << (rank - 1); // 2^(rank-1) pages
    return idx ^ block_size;
}

// Helper: Check if rank is valid
static inline int is_valid_rank(int rank) {
    return rank >= 1 && rank <= MAX_RANK;
}

// Initialize the buddy allocator
int init_page(void *p, int pgcount) {
    base_ptr = p;
    total_pages = pgcount;

    // Initialize free lists
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    // Initialize page ranks
    for (int i = 0; i < total_pages; i++) {
        page_ranks[i] = 0; // 0 means free
    }

    // Add all memory as largest possible blocks
    int idx = 0;
    while (idx < pgcount) {
        // Find largest rank that fits
        int rank = MAX_RANK;
        int block_size = 1 << (rank - 1);

        while (rank > 0 && (idx + block_size > pgcount || (idx % block_size) != 0)) {
            rank--;
            block_size = 1 << (rank - 1);
        }

        if (rank > 0) {
            // Add this block to free list
            free_node_t *node = (free_node_t *)get_page_ptr(idx);
            node->next = free_lists[rank];
            free_lists[rank] = node;
            idx += block_size;
        } else {
            idx++; // shouldn't happen if pgcount is power of 2
        }
    }

    return OK;
}

// Allocate pages of given rank
void *alloc_pages(int rank) {
    if (!is_valid_rank(rank)) {
        return ERR_PTR(-EINVAL);
    }

    // Find a free block of the requested rank or larger
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }

    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC); // No space available
    }

    // Get the block
    void *block = (void *)free_lists[current_rank];
    free_lists[current_rank] = free_lists[current_rank]->next;

    // Split the block down to the requested rank
    while (current_rank > rank) {
        current_rank--;
        int block_size = 1 << (current_rank - 1); // pages
        void *buddy = (char *)block + block_size * PAGE_SIZE;

        // Add buddy to free list
        free_node_t *node = (free_node_t *)buddy;
        node->next = free_lists[current_rank];
        free_lists[current_rank] = node;
    }

    // Mark pages as allocated
    int idx = get_page_index(block);
    int block_size = 1 << (rank - 1);
    for (int i = 0; i < block_size; i++) {
        page_ranks[idx + i] = (char)rank;
    }

    return block;
}

// Free pages back to buddy system
int return_pages(void *p) {
    if (p == NULL) {
        return -EINVAL;
    }

    int idx = get_page_index(p);
    if (idx < 0) {
        return -EINVAL;
    }

    int rank = page_ranks[idx];
    if (rank == 0) {
        return -EINVAL; // Already free
    }

    if (!is_valid_rank(rank)) {
        return -EINVAL;
    }

    // Mark pages as free
    int block_size = 1 << (rank - 1);
    for (int i = 0; i < block_size; i++) {
        page_ranks[idx + i] = 0;
    }

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_index(idx, rank);

        // Check if buddy is valid
        if (buddy_idx < 0 || buddy_idx >= total_pages) {
            break; // No valid buddy
        }

        // Check if the starting index is aligned for the next rank
        int next_block_size = 1 << rank; // 2^rank pages
        if (idx % next_block_size != 0 && buddy_idx % next_block_size != 0) {
            break; // Neither is aligned for next rank
        }

        // Check if buddy exists in free list and remove it
        void *buddy_ptr = get_page_ptr(buddy_idx);
        free_node_t **curr = &free_lists[rank];
        int found = 0;

        while (*curr != NULL) {
            if ((void *)(*curr) == buddy_ptr) {
                *curr = (*curr)->next; // Remove from list
                found = 1;
                break;
            }
            curr = &((*curr)->next);
        }

        if (!found) {
            break; // Buddy not free at this rank
        }

        // Merge: use the lower index
        if (idx > buddy_idx) {
            idx = buddy_idx;
            p = get_page_ptr(idx);
        }
        rank++;
    }

    // Add merged block to free list
    free_node_t *node = (free_node_t *)p;
    node->next = free_lists[rank];
    free_lists[rank] = node;

    return OK;
}

// Query the rank of a page
int query_ranks(void *p) {
    int idx = get_page_index(p);
    if (idx < 0) {
        return -EINVAL;
    }

    // If allocated, return its rank
    if (page_ranks[idx] != 0) {
        return page_ranks[idx];
    }

    // If free, find the maximum rank it belongs to
    // Check each rank's free list
    for (int rank = MAX_RANK; rank >= 1; rank--) {
        int block_size = 1 << (rank - 1);

        // Check if this page is at the start of a block of this rank
        if (idx % block_size == 0) {
            // Check if this block is in the free list
            free_node_t *curr = free_lists[rank];
            while (curr != NULL) {
                if (get_page_index((void *)curr) == idx) {
                    return rank;
                }
                curr = curr->next;
            }
        }
    }

    return -EINVAL; // Shouldn't reach here
}

// Count free pages of given rank
int query_page_counts(int rank) {
    if (!is_valid_rank(rank)) {
        return -EINVAL;
    }

    int count = 0;
    free_node_t *curr = free_lists[rank];
    while (curr != NULL) {
        count++;
        curr = curr->next;
    }

    return count;
}
