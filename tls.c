#include "tls.h"
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

// defined previously in projects
#define MAX_THREAD_COUNT 128

/*
- creates local storage area for the current thread of a given size
- returns 0 on success, -1 on failure
*/
/* global variables */
int initalized = 0;
long int page_size = 0;

// GIVEN
typedef struct page
{
    uintptr_t address;                              // start address of page
    int ref_count;                                  // counter for shared pages
    /* ref_count = 1 for not shared : probably CoW */
} page;

// GIVEN
// data structure to hold the TLS
typedef struct thread_local_storage
{
    pthread_t tid;                                  // thread id
    unsigned int size;                              // size in bytes
    unsigned int page_num;                          // number of pages
    page **pages;                                   // array of pointers to pages
} TLS;

// GIVEN
// mapping of thread to a TLS
struct hash_element
{
    pthread_t tid;
    TLS *tls;
    struct hash_element *next;
} hash_element;

// GIVEN
// This doesn't seem to be the best way of hashing, but less error-prone perhaps
struct hash_element* hash_table[MAX_THREAD_COUNT];

// GIVEN
// handle SIGSEGV and SIGBUS signals
void tls_handle_page_fault(int sig, siginfo_t *si, void *context)
{
    unsigned long int p_fault = ((unsigned long int) si->si_addr) & ~(page_size - 1);

    /* check whether real segfault or because thread has touched forbidden memory */
    // brute force scan through all allocated TLS regions
    int i, j;
    // OUTER LOOP: iterate through all slots
    for(i = 0; i < MAX_THREAD_COUNT; i++)
    {
        if(hash_table[i] != NULL)
        {
            struct hash_element* current = hash_table[i];
            // INNER LOOP: iterate through all elements in the slot -- in case of collisions
            while(current != NULL)
            {
                // figuring out which page caused the fault
                for(j = 0; j < current->tls->page_num; j++)
                {
                    if(current->tls->pages[j]->address == p_fault)
                    {
                        pthread_exit(NULL);
                        return;
                    }
                }
                current = current->next;
            }
        }
    }

    /* normal fault - install default handler and re-raise signal */
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
    return;
}

// GIVEN
// initalize on first run
void tls_init()
{
    struct sigaction sigact;

    /* get size of a page */
    page_size = getpagesize();

    /* install the signal handler for page faults (SIGSEGV, SIGBUS) */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;                   // use extended signal handling
    sigact.sa_sigaction = tls_handle_page_fault;

    /* installing signal handler
    SIGBUS: access to invalid address (dereferencing a pointer not aligned)
    SIGSEGV: access to memory not allowed (permissions)
    */
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);

    // TLS initalized
    initalized = 1;
}

// creates a local storage area for the current thread of a given size
int tls_create(unsigned int size)
{
    if(!initalized)
    {
        initalized = 1;
        tls_init();
    }

    // check if current thread already has LSA, size > 0 or not
    pthread_t TID = pthread_self();
    TLS *checkTLS = NULL;
    struct hash_element* current = NULL;
    int i;
    // iterate through all slots
    for(i = 0; i < MAX_THREAD_COUNT; ++i)
    {
        current = hash_table[i];
        // iterate through all elements in the slot -- in case of collisions
        while(current != NULL)
        {
            // check if current thread already has LSA
            if(current->tid == TID)
            {
                checkTLS = current->tls;
                break;
            }
            current = current->next;
        }

        if(hash_table[i] != NULL && hash_table[i]->tid == TID)
        {
            checkTLS = hash_table[i]->tls;
            break;
        }
    }
    // error: thread already has a LSA with size larger than 0 bytes
    if(checkTLS != NULL && checkTLS->size > 0)
    {
        return -1;
    }

    // allocate TLS with calloc
    TLS* tls = (TLS *) calloc(1, sizeof(TLS));
    if(tls == NULL)
    {
        return -1;
    }
    // initialize TLS
    tls->tid = TID;
    tls->size = size;
    // number of pages needed, if size is not a multiple of page_size, add 1 to account for rest
    tls->page_num = (size % page_size == 0) ? size / page_size : size / page_size + 1;
    // allocate TLS->pages, array of pointers using calloc
    tls->pages = (page **) calloc(tls->page_num, sizeof(page *));
    if(tls->pages == NULL)
    {
        return -1;
    }
    // allocate all pages for this TLS
    int j;
    for(j = 0; j < tls->page_num; j++)
    {
        // allocate page
        page* p = (page *) malloc(sizeof(page));
        p->address = (uintptr_t) mmap(0, page_size, PROT_NONE, (MAP_ANON | MAP_PRIVATE), 0, 0);
        // ref_count starts as one, as it's not shared
        p->ref_count = 1;
        tls->pages[j] = p;
    }

    // add threadid and TLS mapping to global data structure
    struct hash_element* mapping = (struct hash_element *) malloc(sizeof(struct hash_element));

    mapping->tid = TID;
    mapping->tls = tls;
    int table_index = TID % MAX_THREAD_COUNT;
    // no collision
    if(hash_table[table_index] == NULL)
    {
        hash_table[table_index] = mapping;
    }
    // collision
    else
    {
        // linked list method to deal with collisions
        struct hash_element* head = hash_table[table_index];
        hash_table[table_index] = mapping;
        mapping->next = head;
    }
    return 0;
}

// GIVEN
// HELPER FUNCTION
void tls_protect(struct page *p)
{
    // protect the page
    if (mprotect((void *) p->address, page_size, 0))
    {
        fprintf(stderr, "tls_protect: could not protect page\n");
        exit(1);
    }
}

// GIVEN
// HELPER FUNCTION
void tls_unprotect(struct page *p)
{
    // unprotect the page
    if (mprotect((void *) p->address, page_size, PROT_READ | PROT_WRITE))
    {
        fprintf(stderr, "tls_unprotect: could not unprotect page\n");
        exit(1);
    }
}

// reads length bytes from buffer->memloc, writes to LSA oof current thread at position offset
int tls_write(unsigned int offset, unsigned int length, char *buffer)
{
    /* error handling */
    // check if the current thread has a LSA
    pthread_t TID = pthread_self();
    int table_index = TID % MAX_THREAD_COUNT;
    struct hash_element* current = hash_table[table_index];
    // iterate through all elements in the slot -- in case of collisions
    if(current != NULL)
    {
        while(current != NULL)
        {
            if(current->tid == TID)
            {
                break;
            }
            current = current->next;
        }
    }
    else
    {
        return -1;
    }
    // TLS for current thread doesn't exist
    if(current->tid != TID)
    {
        return -1;
    }

    // check if offset+length > size
    if((offset + length) > current->tls->size)
    {
        return -1;
    }

    /* unprotect all pages belonging to thread's TLS */
    int i;
    for(i = 0; i < current->tls->page_num; i++)
    {
        tls_unprotect(current->tls->pages[i]);
    }

    /* perform the write operation */
    int count, idx;
    for (count = 0, idx = offset; idx < (offset + length); ++count, ++idx)
    {
        // find the page and offset
        page *p, *copy;
        unsigned int pn, poff;
        pn = idx / page_size;
        poff = idx % page_size;
        p = current->tls->pages[pn];
        if (p->ref_count > 1)
        {
            /* this page is shared, create a private copy (CoW) */
            copy = (struct page *) calloc(1, sizeof(struct page));
            copy->address = (unsigned long int) mmap(0, page_size, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);

            memcpy((void *) copy->address, (void *) p->address, page_size);
            // ref_count starts as one, as it's not shared
            copy->ref_count = 1;
            current->tls->pages[pn] = copy;

            /* update original page */
            p->ref_count = (p->ref_count) - 1;
            // protect the original page
            tls_protect(p);
            p = copy;
        }
        // write to the page
        char* dst = ((char *) p->address) + poff;
        *dst = buffer[count];
    }

    // reprotect all pages belonging to thread's TLS
    for (i = 0; i < current->tls->page_num; i++)
    {
        tls_protect(current->tls->pages[i]);
    }
    return 0;
}

// reads length bytes from LSA of thread at offset, writes into buffer->memloc
int tls_read(unsigned int offset, unsigned int length, char *buffer)
{   /* error handling */
    // check if current thread has TLS
    pthread_t TID = pthread_self();
    int table_index = TID % MAX_THREAD_COUNT;
    struct hash_element* current = hash_table[table_index];
    // iterate through all elements in the slot -- in case of collisions
    if(current != NULL)
    {
        // iterate through all elements in the slot -- in case of collisions
        while(current != NULL)
        {
            if(current->tid == TID)
            {
                break;
            }
            current = current->next;
        }
        // TLS for current thread doesn't exist
        if(current == NULL)
        {
            return -1;
        }

        // check if offset+length > size
        if((offset + length) > current->tls->size)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    // unprotect all pages belonging to thread
    int i;
    for(i = 0; i < current->tls->page_num; i++)
    {
        tls_unprotect(current->tls->pages[i]);
    }

    // peform read operation
    int count;
    int idx;
    for(count = 0, idx = offset; idx < (offset + length); ++count, ++idx)
    {
        page *p;
        unsigned int pn, poff;

        pn = idx / page_size;
        poff = idx % page_size;

        p = current->tls->pages[pn];
        char* src = ((char *) p->address) + poff;

        buffer[count] = *src;
    }
    // reprotect all pages belonigng to thread's TLS
    for(i = 0; i < current->tls->page_num; i++)
    {
        tls_protect(current->tls->pages[i]);
    }

    return 0;
}

// frees LAS of current thread
int tls_destroy()
{
    // error handling: check if current thread has TLS
    pthread_t TID = pthread_self();
    int table_index = TID % MAX_THREAD_COUNT;
    struct hash_element *current = hash_table[table_index];

    // remove TLS from global data structure, relink list
    if(hash_table[table_index] != NULL)
    {
        struct hash_element* cur = hash_table[table_index];
        while(cur != NULL)
        {
            // found the TLS
            if(cur->tid == TID)
            {
                break;
            }
            // move to next element
            cur = cur->next;
        }
        current = cur;
    }
    else
    {
        return -1;
    }

    /* clean up all pages */
    // if not shared, free the page, if shared, can't free as other threads rely on it
    int i;
    for(i = 0; i < current->tls->page_num; i++)
    {
        /* check each page whether it's shared */
        // shared, cannot free as others rely, deccrement instead
        if(current->tls->pages[i]->ref_count > 1)
        {
            current->tls->pages[i]->ref_count = (current->tls->pages[i]->ref_count) - 1;
        }
        // if not shared, free the page
        else
        {
            // unmap the page
            munmap((void *) current->tls->pages[i]->address, page_size);
            free(current->tls->pages[i]);
        }
    }

    /* free TLS */
    free(current->tls->pages);
    free(current->tls);
    free(current);
    return 0;
}

// clones LSA of target thread, when TLSA is cloned, content isnt copied rather storage area refer to same memloc, CoW
int tls_clone(pthread_t tid)
{
    /*error handling */

    int target_index = tid % MAX_THREAD_COUNT;
    pthread_t clone_TID = pthread_self();
    int clone_index = clone_TID % MAX_THREAD_COUNT;

    struct hash_element *new_entry = NULL;
    struct hash_element* target_entry = NULL;

    // check if current thread already has LSA. if so, return -1; cannot clone into a thread that already has a LSA
    new_entry = hash_table[clone_index];
    // iterate through all elements in the slot -- in case of collisions
    while(new_entry != NULL)
    {
        if(new_entry->tid == clone_TID)
        {
            return -1;
        }
        new_entry = new_entry->next;
    }
    // already exists with a TLS mapping
    if(new_entry != NULL)
    {
        return -1;
    }

    // find target thread LSA
    target_entry = hash_table[target_index];
    while(target_entry != NULL)
    {
        if(target_entry->tid == tid)
        {
            break;
        }
        target_entry = target_entry->next;
    }
    // no TLS, nothing to clone from
    if(target_entry == NULL)
    {
        return -1;
    }

    // allocate TLS for thread clone
    new_entry = (struct hash_element *) calloc(1, sizeof(struct hash_element));
    if(new_entry == NULL)
    {
        return -1;
    }
    // copy thread id
    new_entry->tid = clone_TID;
    new_entry->tls = (TLS *) calloc(1, sizeof(TLS));
    if(new_entry->tls == NULL)
    {
        return -1;
    }

    // copy relevant TLS information from target thread
    new_entry->tls->page_num = target_entry->tls->page_num;
    new_entry->tls->size = target_entry->tls->size;
    // allocate pages
    new_entry->tls->pages = (page **) calloc(target_entry->tls->page_num, sizeof(page *));
    if(new_entry->tls->pages == NULL)
    {
        return -1;
    }

    // copy pages
    int i;
    for(i = 0; i < target_entry->tls->page_num; i++)
    {
        // point the same place in memory
        new_entry->tls->pages[i] = target_entry->tls->pages[i];
        // reference count update
        (new_entry->tls->pages[i]->ref_count)++;
    }

    // add entry to global data structure
    if(hash_table[clone_index] == NULL)
    {
        // no collision
        hash_table[clone_index] = new_entry;
    }
    else
    {
        // linked list method to deal with collisions
        struct hash_element* temp = hash_table[clone_index];
        hash_table[clone_index] = new_entry;
        hash_table[clone_index]->next = temp;
    }
    return 0;