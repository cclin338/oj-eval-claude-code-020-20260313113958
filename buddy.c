#include "buddy.h"
#define NULL ((void *)0)

#define PAGE_SIZE (4 * 1024)
#define MAX_RANK 16
#define MAX_PAGES (128 * 1024 / 4) // Maximum pages we might need to track

// Free list node
typedef struct free_node {
    struct free_node *next;
} free_node_t;

// Block metadata
typedef struct {
    char allocated;  // 0 = free, 1 = allocated
    char rank;       // rank of the block (for both allocated and free blocks)
} block_info_t;

// Global variables
static void *base_ptr = NULL;
static int total_pages = 0;
static free_node_t *free_lists[MAX_RANK + 1]; // free_lists[1] to free_lists[16]
static block_info_t block_info[MAX_PAGES]; // Track info for each page

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

    // Initialize block info
    for (int i = 0; i < total_pages; i++) {
        block_info[i].allocated = 0;
        block_info[i].rank = 0;
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

            // Mark block as free with this rank
            block_info[idx].allocated = 0;
            block_info[idx].rank = rank;

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

    int idx = get_page_index(block);

    // Split the block down to the requested rank
    while (current_rank > rank) {
        current_rank--;
        int block_size = 1 << (current_rank - 1); // pages
        int buddy_idx = idx + block_size;
        void *buddy = get_page_ptr(buddy_idx);

        // Add buddy to free list
        free_node_t *node = (free_node_t *)buddy;
        node->next = free_lists[current_rank];
        free_lists[current_rank] = node;

        // Mark buddy as free
        block_info[buddy_idx].allocated = 0;
        block_info[buddy_idx].rank = current_rank;
    }

    // Mark block as allocated
    block_info[idx].allocated = 1;
    block_info[idx].rank = rank;

    return block;
}

// Remove a specific block from free list
static void remove_from_free_list(int rank, int idx) {
    void *target = get_page_ptr(idx);
    free_node_t **curr = &free_lists[rank];

    while (*curr != NULL) {
        if ((void *)(*curr) == target) {
            *curr = (*curr)->next;
            return;
        }
        curr = &((*curr)->next);
    }
}

// Free pages back to buddy system
int return_pages(void *p) {
    if (p == NULL) {
        return -EINVAL;
    }

    int idx = get_page_index(p);
    if (idx < 0 || idx >= total_pages) {
        return -EINVAL;
    }

    if (!block_info[idx].allocated) {
        return -EINVAL; // Already free
    }

    int rank = block_info[idx].rank;
    if (!is_valid_rank(rank)) {
        return -EINVAL;
    }

    // Mark block as free (but don't add to list yet, we'll merge first)
    block_info[idx].allocated = 0;

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_index(idx, rank);

        // Check if buddy is valid and within bounds
        if (buddy_idx < 0 || buddy_idx >= total_pages) {
            break;
        }

        // Check if buddy is free and at the same rank
        if (block_info[buddy_idx].allocated || block_info[buddy_idx].rank != rank) {
            break;
        }

        // Remove buddy from free list
        remove_from_free_list(rank, buddy_idx);

        // Merge: use the lower index
        if (idx > buddy_idx) {
            idx = buddy_idx;
        }

        rank++;
    }

    // Add merged block to free list
    void *block = get_page_ptr(idx);
    free_node_t *node = (free_node_t *)block;
    node->next = free_lists[rank];
    free_lists[rank] = node;

    // Update block info
    block_info[idx].allocated = 0;
    block_info[idx].rank = rank;

    return OK;
}

// Query the rank of a page
int query_ranks(void *p) {
    int idx = get_page_index(p);
    if (idx < 0 || idx >= total_pages) {
        return -EINVAL;
    }

    return block_info[idx].rank;
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
