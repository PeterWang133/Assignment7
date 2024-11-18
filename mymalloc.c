#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

typedef struct node {
    size_t size;
    bool free_flag;
    struct node* next;
} node_t;

// Mutex for thread safety
pthread_mutex_t allocator_lock = PTHREAD_MUTEX_INITIALIZER;
node_t *head = NULL;

void *mymalloc(size_t size) {
    pthread_mutex_lock(&allocator_lock);
    node_t *current = head;
    node_t *prev = NULL;

    size = (size + 7) & ~7; // Align size

    while (current != NULL) {
        if (current->free_flag && current->size >= size) {
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
            pthread_mutex_unlock(&allocator_lock);
            return (void *)(current + 1);
        }
        prev = current;
        current = current->next;
    }

    size_t total_size = size + sizeof(node_t);
    size_t alloc_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        pthread_mutex_unlock(&allocator_lock);
        return NULL;
    }

    node_t *new_block = (node_t *)ptr;
    new_block->size = alloc_size - sizeof(node_t);
    new_block->free_flag = false;
    new_block->next = NULL;

    if (prev == NULL) {
        head = new_block;
    } else {
        prev->next = new_block;
    }

    pthread_mutex_unlock(&allocator_lock);
    return (void *)(new_block + 1);
}

void myfree(void *ptr) {
    if (!ptr) return;

    pthread_mutex_lock(&allocator_lock);
    node_t *block = (node_t *)ptr - 1;
    block->free_flag = true;

    node_t *current = head;
    while (current != NULL) {
        if (current->free_flag && current->next && current->next->free_flag) {
            current->size += sizeof(node_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
    pthread_mutex_unlock(&allocator_lock);
}

void *mycalloc(size_t nmemb, size_t s) {
    size_t total_size = nmemb * s;
    void *p = mymalloc(total_size);
    if (!p) return NULL;
    memset(p, 0, total_size);
    return p;
}
