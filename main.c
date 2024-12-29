#include <stdio.h>
#include <pthread.h>

void* thread_function(void *arg) {
    // Clone the TLS from the main thread
    pthread_t main_thread_id = *(pthread_t*)arg;
    int clone_result = tls_clone(main_thread_id);
    if (clone_result == 0) {
        printf("tls_clone succeeded in new thread.\n");
    } else {
        printf("tls_clone failed in new thread.\n");
        return (void*)1; // Indicate failure
    }

    // Destroy the TLS in the new thread
    int destroy_result = tls_destroy();
    if (destroy_result == 0) {
        printf("tls_destroy succeeded in new thread.\n");
    } else {
        printf("tls_destroy failed in new thread.\n");
    }

    return (void*)0; // Indicate success
}

int main() {
    printf("Starting TLS test with tls_create, tls_clone, and tls_destroy.\n");

    // Create TLS in the main thread
    unsigned int tls_size = 4096; // Example size
    int create_result = tls_create(tls_size);
    if (create_result == 0) {
        printf("tls_create succeeded in main thread.\n");
    } else {
        printf("tls_create failed in main thread.\n");
        return 1; // Exit if TLS creation failed
    }

    // Start a new thread and clone the TLS
    pthread_t new_thread, main_thread_id = pthread_self();
    if (pthread_create(&new_thread, NULL, thread_function, &main_thread_id) != 0) {
        printf("Failed to create new thread.\n");
        return 1;
    }

    // Wait for the new thread to complete
    void *thread_return;
    pthread_join(new_thread, &thread_return);
    if (thread_return != (void*)0) {
        printf("New thread encountered an error.\n");
        return 1;
    }
    // Destroy the TLS in the main thread
    int destroy_result = tls_destroy();
        printf("Test for seg fault\n");
    if (destroy_result == 0) {
        printf("tls_destroy succeeded in main thread.\n");
    } else {
        printf("tls_destroy failed in main thread.\n");
    }

    return 0;
}