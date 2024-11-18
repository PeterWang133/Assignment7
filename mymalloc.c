#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include <debug.h> // definition of debug_printf

// Node structure for memory block metadata
typedef struct node {
    size_t size;
    bool free_flag;
    struct node* next;
} node_t;

// Head of the memory block linked list
static node_t *head = NULL;

// Mutex for thread safety
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Page size for mmap allocations
static const size_t PAGE_SIZE = 4096;

// Align size to the nearest multiple of alignment
static size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// Allocate memory using mmap
static void* allocate_memory(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    return ptr;
}

// Free memory using munmap
static void deallocate_memory(void* ptr, size_t size) {
    if (munmap(ptr, size) == -1) {
        perror("munmap failed");
    }
}

// Find a suitable free block or return NULL
static node_t* find_free_block(size_t size) {
    node_t* current = head;
    while (current != NULL) {
        if (current->free_flag && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Split a block if it's significantly larger than requested
static void split_block(node_t* block, size_t size) {
    if (block->size >= size + sizeof(node_t) + PAGE_SIZE) {
        node_t* new_block = (node_t*)((char*)(block + 1) + size);
        new_block->size = block->size - size - sizeof(node_t);
        new_block->free_flag = true;
        new_block->next = block->next;

        block->size = size;
        block->next = new_block;
    }
}

// Coalesce adjacent free blocks
static void coalesce_free_blocks() {
    node_t* current = head;
    while (current != NULL && current->next != NULL) {
        if (current->free_flag && current->next->free_flag) {
            current->size += sizeof(node_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

// Custom malloc function
void* mymalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    pthread_mutex_lock(&mutex);

    size = align_size(size, sizeof(void*));

    node_t* block = find_free_block(size);
    if (block != NULL) {
        block->free_flag = false;
        split_block(block, size);
        pthread_mutex_unlock(&mutex);
        return (void*)(block + 1);
    }

    size_t total_size = size + sizeof(node_t);
    if (total_size < PAGE_SIZE) {
        total_size = PAGE_SIZE;
    } else {
        total_size = align_size(total_size, PAGE_SIZE);
    }

    void* ptr = allocate_memory(total_size);
    if (ptr == NULL) {
        pthread_mutex_unlock(&mutex);
        return NULL;
    }

    block = (node_t*)ptr;
    block->size = total_size - sizeof(node_t);
    block->free_flag = false;
    block->next = NULL;

    if (head == NULL) {
        head = block;
    } else {
        node_t* current = head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = block;
    }

    pthread_mutex_unlock(&mutex);
    return (void*)(block + 1);
}

// Custom calloc function
void* mycalloc(size_t nmemb, size_t s) {
    size_t total_size = nmemb * s;
    void* ptr = mymalloc(total_size);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

// Custom free function
void myfree(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    pthread_mutex_lock(&mutex);

    node_t* block = (node_t*)ptr - 1;
    block->free_flag = true;

    coalesce_free_blocks();

    pthread_mutex_unlock(&mutex);
}
