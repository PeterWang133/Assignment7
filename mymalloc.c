#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <malloc.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <debug.h>

#define MIN_BLOCK_SIZE 24
#define ALIGNMENT 8

typedef struct node {
    size_t size;
    bool free_flag;
    struct node* next;
    struct node* prev;
} node_t;

static node_t *head = NULL;

// Align size to 8 bytes
static size_t align_size(size_t size) {
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}

// Check if two blocks are adjacent in memory
static bool are_blocks_adjacent(node_t *first, node_t *second) {
    return (char *)(first + 1) + first->size == (char *)second;
}

// Find a free block using first-fit
static node_t* find_free_block(size_t size) {
    node_t *current = head;
    while (current != NULL) {
        if (current->free_flag && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Split a block if the remaining size is sufficient
static void split_block(node_t *block, size_t requested_size) {
    size_t remaining_size = block->size - requested_size;
    
    if (remaining_size >= MIN_BLOCK_SIZE + sizeof(node_t)) {
        node_t *new_block = (node_t *)((char *)(block + 1) + requested_size);
        new_block->size = remaining_size - sizeof(node_t);
        new_block->free_flag = true;
        new_block->next = block->next;
        new_block->prev = block;
        
        if (block->next) {
            block->next->prev = new_block;
        }
        
        block->size = requested_size;
        block->next = new_block;
    }
}

void *mymalloc(size_t size) {
    if (size == 0) return NULL;
    
    size = align_size(size);
    
    // Try to find a free block
    node_t *block = find_free_block(size);
    if (block != NULL) {
        block->free_flag = false;
        split_block(block, size);
        debug_printf("Reused block of size %zu\n", block->size);
        return (void *)(block + 1);
    }
    
    // No suitable block found, request new memory
    size_t total_size = sizeof(node_t) + size;
    void *ptr = sbrk(total_size);
    if (ptr == (void *)-1) return NULL;
    
    // Initialize new block
    block = (node_t *)ptr;
    block->size = size;
    block->free_flag = false;
    block->next = NULL;
    block->prev = NULL;
    
    // Add to linked list
    if (head == NULL) {
        head = block;
    } else {
        node_t *current = head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = block;
        block->prev = current;
    }
    
    debug_printf("Allocated new block of size %zu\n", size);
    return (void *)(block + 1);
}

void *mycalloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    if (size != 0 && total_size / size != nmemb) return NULL;
    
    void *ptr = mymalloc(total_size);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
        debug_printf("Calloc %zu bytes\n", total_size);
    }
    return ptr;
}

// Coalesce only if blocks are adjacent
static void coalesce_blocks(node_t *block) {
    // Coalesce with previous block if adjacent
    if (block->prev && block->prev->free_flag && are_blocks_adjacent(block->prev, block)) {
        block->prev->size += sizeof(node_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
        debug_printf("Coalesced with previous block\n");
        block = block->prev;  // Update block to merged block
    }
    
    // Coalesce with next block if adjacent
    if (block->next && block->next->free_flag && are_blocks_adjacent(block, block->next)) {
        block->size += sizeof(node_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
        debug_printf("Coalesced with next block\n");
    }
}


void myfree(void *ptr) {
    if (ptr == NULL) return;
    
    node_t *block = ((node_t *)ptr) - 1;
    
    // Validate pointer to ensure it's part of our allocated list
    node_t *current = head;
    bool valid = false;
    while (current != NULL) {
        if (current == block) {
            valid = true;
            break;
        }
        current = current->next;
    }
    
    if (!valid) {
        debug_printf("Invalid free pointer\n");
        return;
    }
    
    // Mark block as free and attempt coalescing
    block->free_flag = true;
    debug_printf("Freed block of size %zu\n", block->size);
    
    // Coalesce adjacent blocks if possible
    coalesce_blocks(block);
}
