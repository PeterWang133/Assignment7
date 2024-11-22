/**
 * This file implements a thread-safe memory allocator using mmap and a free list.
 * It supports dynamic memory allocation with features including:
 * - Block splitting for efficient memory usage
 * - Coalescing of free blocks
 * - Support for small and large memory allocations
 * - Thread-safe operations using mutex
 */

// importing neccesary libararies
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

#define PAGE_SIZE 4096 // system page size

/**
 * Memory block metadata structure
 * Tracks allocation details and links blocks in free list
 */
typedef struct node {
    size_t size;
    bool free_flag;
    struct node* next;
} node_t;

// Mutex for thread safety
pthread_mutex_t allocator_lock = PTHREAD_MUTEX_INITIALIZER;
node_t *head = NULL;

/**
 * Allocates memory with thread-safe mechanisms
 * 
 * @param size Requested memory size in bytes
 * @return Pointer to allocated memory or NULL if allocation fails
 * 
 * - For small allocations (<PAGE_SIZE):
 *   1. Search free list for suitable block
 *   2. Split block if significantly larger than request
 *   3. Allocate new page if no suitable block exists
 * - For large allocations (≥PAGE_SIZE):
 *   1. Allocate multiple pages using mmap
 */
void *mymalloc(size_t size) {
    // Initial input validation and size alignment
    if (size == 0) return NULL;

    pthread_mutex_lock(&allocator_lock);
    
    // Minimum allocation size
    size = (size < sizeof(void*)) ? sizeof(void*) : size;
    size = (size + 7) & ~7; // Align size

    // Large allocation: directly map memory using mmap
    if (size >= PAGE_SIZE) {
        size_t total_size = size + sizeof(node_t);
        size_t pages_needed = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t alloc_size = pages_needed * PAGE_SIZE;

        void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            pthread_mutex_unlock(&allocator_lock);
            return NULL;
        }

        // Create and configure metadata for large block
        node_t *large_block = (node_t *)ptr;
        large_block->size = alloc_size - sizeof(node_t);
        large_block->free_flag = false;
        large_block->next = NULL;

        pthread_mutex_unlock(&allocator_lock);
        return (void *)(large_block + 1);
    }

    // Small allocation: search and potentially split free list blocks
    node_t *current = head;
    node_t *prev = NULL;

    while (current != NULL) {
        // Find suitable free block and potentially split it
        if (current->free_flag && current->size >= size) {
            // Detailed splitting logic
            if (current->size >= size + sizeof(node_t) + 8) {
                node_t *new_block = (node_t *)((char *)(current + 1) + size);
                new_block->size = current->size - size - sizeof(node_t);
                new_block->free_flag = true;
                new_block->next = current->next;

                current->size = size;
                current->free_flag = false;
                current->next = new_block;

                // Insert new block into free list
                if (prev == NULL) {
                    head = new_block;
                } else {
                    prev->next = new_block;
                }
            } else {
                current->free_flag = false;
            }
            pthread_mutex_unlock(&allocator_lock);
            return (void *)(current + 1);
        }
        prev = current;
        current = current->next;
    }

    // No suitable block: allocate new page
    size_t total_size = size + sizeof(node_t);
    size_t alloc_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        pthread_mutex_unlock(&allocator_lock);
        return NULL;
    }

    // Create new block and link to free list
    node_t *new_block = (node_t *)ptr;
    new_block->size = alloc_size - sizeof(node_t);
    new_block->free_flag = false;
    new_block->next = head;
    head = new_block;

    pthread_mutex_unlock(&allocator_lock);
    return (void *)(new_block + 1);
}

/**
 * Frees previously allocated memory
 * 
 * @param ptr Pointer to memory block to be freed
 * 
 * - For small blocks: 
 *   1. Mark block as free
 *   2. Coalesce adjacent free blocks
 * - For large blocks:
 *   1. Unmap memory using munmap
 */
void myfree(void *ptr) {
    if (!ptr) return;

    pthread_mutex_lock(&allocator_lock);
    node_t *block_to_free = (node_t *)ptr - 1;
    
    // Validate block before freeing
    if (block_to_free->free_flag) {
        printf("Double free detected!\n");
        pthread_mutex_unlock(&allocator_lock);
        return;
    }

    // Large allocation handling remains the same
    if (block_to_free->size >= PAGE_SIZE) {
        munmap(block_to_free, block_to_free->size + sizeof(node_t));
        pthread_mutex_unlock(&allocator_lock);
        return;
    }

    block_to_free->free_flag = true;

    // More robust coalescing
    node_t *current = head;
    node_t *prev = NULL;

    while (current != NULL) {
        // Advanced coalescing logic
        if (current->free_flag) {
            // Check if current can merge with next block
            if (current->next && 
                current->next->free_flag && 
                (char*)current + sizeof(node_t) + current->size == (char*)current->next) {
                
                current->size += sizeof(node_t) + current->next->size;
                current->next = current->next->next;
                continue;  // Restart check
            }

            // Check if previous block can merge with current
            if (prev && prev->free_flag && 
                (char*)prev + sizeof(node_t) + prev->size == (char*)current) {
                
                prev->size += sizeof(node_t) + current->size;
                prev->next = current->next;
                current = prev;
                continue;
            }
        }

        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&allocator_lock);
}

/**
 * Allocates and initializes memory to zero
 * 
 * @param nmemb Number of elements
 * @param s Size of each element
 * @return Pointer to zeroed memory block
 */
void *mycalloc(size_t nmemb, size_t s) {
    size_t total_size = nmemb * s; // Calculate total memory size needed by multiplying number of elements and element size
    void *p = mymalloc(total_size); // Allocate memory
    if (!p) return NULL;
    memset(p, 0, total_size);// Initialize allocated memory to zero using memset
    return p;
}

// Simple thread function to test allocator
void* thread_allocate(void* arg) {
    int thread_id = *(int*)arg;
    void* allocated_ptrs[20];
    
    for (int i = 0; i < 10; i++) {
        size_t size = (thread_id + i + 1) * 100;
        
        void* ptr = mymalloc(size);
        if (ptr == NULL) {
            printf("Thread %d failed to allocate %zu bytes\n", thread_id, size);
            return NULL;
        }

        // Verify unique memory content
        memset(ptr, thread_id, size);
        
        // Verify memory content
        for (size_t j = 0; j < size; j++) {
            if (((char*)ptr)[j] != thread_id) {
                printf("Memory corruption detected in thread %d\n", thread_id);
                return NULL;
            }
        }

        allocated_ptrs[i] = ptr;
        printf("Thread %d allocated %zu bytes at %p\n", thread_id, size, ptr);
    }

    // Free in reverse order to test different free patterns
    for (int i = 9; i >= 0; i--) {
        myfree(allocated_ptrs[i]);
    }

    return NULL;
}

//main function to run program and test
int main() {
    // Basic allocation tests
    printf("Basic Allocation Test:\n");
    int *int_ptr = mymalloc(sizeof(int));
    *int_ptr = 42;
    printf("Integer allocation: %d\n", *int_ptr);
    myfree(int_ptr);

    // Large allocation test
    printf("\nLarge Allocation Test:\n");
    char *large_ptr = mymalloc(PAGE_SIZE * 2);
    strcpy(large_ptr, "Large memory block test");
    printf("Large block content: %s\n", large_ptr);
    myfree(large_ptr);

    // Threaded allocation test
    const int NUM_THREADS = 5;
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("\nMulti-threaded Allocation Test:\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, thread_allocate, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All tests completed successfully\n");
    return 0;
}