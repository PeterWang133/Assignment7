#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>

#include <debug.h> // definition of debug_printf

#define PAGE_SIZE 4096

typedef struct node {
    size_t size;
    bool free_flag;
    struct node* next;
} node_t;

node_t *head = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *mymalloc(size_t size) {
    pthread_mutex_lock(&lock);

    node_t *current = head;
    node_t *prev = NULL;

    // Align size to the nearest multiple of 8 for better memory alignment
    size = (size + 7) & ~7;

    // Search for a suitable free block
    while (current != NULL) {
        if (current->free_flag && current->size >= size) {
            // Split the block if it's significantly larger than requested
            if (current->size >= size + sizeof(node_t) + 8) {
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
            pthread_mutex_unlock(&lock);
            return (void *)(current + 1);
        }
        prev = current;
        current = current->next;
    }

    // Allocate a new block using mmap
    size_t total_size = size + sizeof(node_t);
    size_t alloc_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); // Round up to nearest page size

    void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    node_t *new_block = (node_t *)ptr;
    new_block->size = alloc_size - sizeof(node_t);
    new_block->free_flag = false;
    new_block->next = NULL;

    // Add the new block to the linked list
    if (prev == NULL) {
        head = new_block;
    } else {
        prev->next = new_block;
    }

    pthread_mutex_unlock(&lock);
    return (void *)(new_block + 1);
}

void *mycalloc(size_t nmemb, size_t s) {
    size_t total_size = nmemb * s;
    void *p = mymalloc(total_size);

    if (!p) {
        return NULL;
    }

    // Zero out the allocated memory
    memset(p, 0, total_size);

    debug_printf("Calloc %zu bytes\n", total_size);
    return p;
}

void myfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    pthread_mutex_lock(&lock);

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

    pthread_mutex_unlock(&lock);
}
