#ifndef TLS_H
#define TLS_H
#include "pthread.h"
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>      // standard buffered input/output
#include <unistd.h>     // standard symbolic constants and types
#include <sys/mman.h>   // memory management declarations
#include <sys/types.h>  // data typess
#include <stdint.h>     //declares integer types having specified widths
#include <fcntl.h>      // file control options


/* create / destroy TLS */
// creates a LSA for the currently executing threadd that can hold at leasst size bytes
int tls_create(unsigned int size);

// destroys the LSA of the currently executing thread
int tls_destroy();

// clones the LSA of the thread with ID tid into the currently executing thread
int tls_clone(pthread_t tid);

/* write to a TLS */
// reads length butes, sarting from the memory locoatioon pointed to by buffer, and writes them into the local storage area of the currently execcuting thread, starting at position offset
int tls_write(unsigned int offset, unsigned int length, char *buffer);

/* read from a TLS */
// reads length bytes from the local storage area of the currently executing thread, starting at position offset, and writes them into the memory location pointed to by buffer
int tls_read(unsigned int offset, unsigned int length, char *buffer);

#endif