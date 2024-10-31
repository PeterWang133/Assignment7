#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <malloc.h> 
#include <stdio.h> 
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include <debug.h> // definition of debug_printf

// Node structure for memory block metadata
typedef struct node {
    int size;
    bool free_flag;
    struct node* next;
} node_t;

node_t *head = NULL;
const int THRESHOLD = 4096;  // Set a threshold for larger allocations to prevent sbrk overuse

void *mymalloc(size_t size) {
    node_t *current = head;
    node_t *prev = NULL;

    // Search for a suitable free block, prioritizing reuse
    while (current != NULL) {
        if (current->free_flag && current->size >= size) {
            // Split the block if it's much larger than requested
            if (current->size >= size + sizeof(node_t) + THRESHOLD) {
                node_t *new_block = (node_t *)((char *)(current + 1) + size);
                new_block->size = current->size - size - sizeof(node_t);
                new_block->free_flag = true;
                new_block->next = current->next;
                
                current->size = size;
                current->free_flag = false;
                current->next = new_block;
            } else {
                current->free_flag = false;
            }
            return (void *)(current + 1);  // Return pointer after the header
        }
        prev = current;
        current = current->next;
    }

    // Allocate a new block if no suitable free block is found
    void *ptr = sbrk(size + sizeof(node_t));
    if (ptr == (void *) -1) {
        return NULL;  // Allocation failed
    }

    node_t *new_block = (node_t *)ptr;
    new_block->size = size;
    new_block->free_flag = false;
    new_block->next = NULL;

    // Add the new block to the linked list
    if (prev == NULL) {
        head = new_block;
    } else {
        prev->next = new_block;
    }

    return (void *)(new_block + 1);  // Return the address after the metadata
}

void *mycalloc(size_t nmemb, size_t s) {
    size_t total_size = nmemb * s;
    void *p = mymalloc(total_size);

    if (!p) {
        return NULL;  // Allocation failed
    }

    // Zero out the allocated memory
    memset(p, 0, total_size);

    debug_printf("calloc %zu bytes\n", total_size);
    return p;
}

void myfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    static bool debug_message_printed = false;
    if (!debug_message_printed) {
        debug_printf("Freed some memory\n");
        debug_message_printed = true;
    }

    node_t *block = (node_t *)ptr - 1;
    block->free_flag = true;

    // Coalesce adjacent free blocks
    node_t *current = head;
    while (current != NULL) {
        if (current->free_flag && current->next && current->next->free_flag) {
            current->size += sizeof(node_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}
