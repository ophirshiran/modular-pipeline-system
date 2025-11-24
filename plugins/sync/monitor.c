#include "monitor.h"


int monitor_init(monitor_t* monitor) {
    if (!monitor) return -1; // Check if monitor is NULL

   if (monitor->is_initialized == 1) return -1; // Check if monitor is already initialized

    // monitor->is_initialized = 0; 
    // monitor->signaled = 0; 

    if (pthread_mutex_init(&monitor->mutex, NULL) != 0) { // Initialize mutex
       // monitor->is_initialized = 0; // Mark monitor as uninitialized
        return -1; // Return -1 on failure
    }
    if (pthread_cond_init(&monitor->condition, NULL) != 0) { // Initialize condition variable
        pthread_mutex_destroy(&monitor->mutex); // Destroy mutex on failure
        // monitor->is_initialized = 0; // Mark monitor as uninitialized
        return -1; // Return -1 on failure
    }

    monitor->signaled = 0; // Initialize signaled flag

    monitor->is_initialized = 1; // Mark monitor as initialized

    return 0; // Return 0 on success
}

void monitor_destroy(monitor_t* monitor) {
 
    if (!monitor || !monitor->is_initialized){ // Check if monitor is NULL or uninitialized
        return; // Nothing to destroy
    }
    pthread_cond_destroy(&monitor->condition); // Destroy condition variable
    pthread_mutex_destroy(&monitor->mutex); // Destroy mutex
    monitor->is_initialized = 0; // Mark monitor as uninitialized
    monitor->signaled = 0; // Reset signaled flag
}

void monitor_signal(monitor_t* monitor) {
    if (!monitor || !monitor->is_initialized) { // Check if monitor is NULL or uninitialized
        return; // Nothing to signal
    }

    pthread_mutex_lock(&monitor->mutex); // Lock mutex

    monitor->signaled = 1; // Set signaled flag
    //pthread_cond_signal(&monitor->condition); // Signal condition variable
     pthread_cond_broadcast(&monitor->condition);

    pthread_mutex_unlock(&monitor->mutex); // Unlock mutex
}

void monitor_reset(monitor_t* monitor) {
    if (!monitor || !monitor->is_initialized) { // Check if monitor is NULL or uninitialized
        return; // Nothing to reset
    }

    pthread_mutex_lock(&monitor->mutex); // Lock mutex

    monitor->signaled = 0; // Reset signaled flag

    pthread_mutex_unlock(&monitor->mutex); // Unlock mutex
}

int monitor_wait(monitor_t* monitor) {
    if (!monitor || !monitor->is_initialized) { // Check if monitor is NULL or uninitialized
        return -1; // Return -1 on error
    }

    pthread_mutex_lock(&monitor->mutex); // Lock mutex

    while (!monitor->signaled) { // Wait until signal is received
        pthread_cond_wait(&monitor->condition, &monitor->mutex); 
    }

    pthread_mutex_unlock(&monitor->mutex); // Unlock mutex

    return 0; // Return 0 on success

}