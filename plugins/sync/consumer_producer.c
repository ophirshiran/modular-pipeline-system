// consumer_producer.c

#include "consumer_producer.h"
#include <pthread.h>
#include <stdlib.h>   /* calloc, free */
#include <stdint.h>   /* SIZE_MAX     */

const char* consumer_producer_init(consumer_producer_t* queue, int capacity) {
    // validate input
    if (queue == NULL)  return "queue is NULL";
    if (capacity <= 0)  return "capacity must be > 0";

    // [Double init guard] 
    // only treat it as already initialized if both
    // the magic and the flag are set.  (avoids false positives on random stack garbage)
    if (queue->magic == CP_MAGIC && queue->is_initialized == 1) {
        return "queue already initialized";
    }

    // Treat this as a fresh struct. It's safe to zero our local monitor
    // structs here and call monitor_init() below.
    queue->not_full_monitor  = (monitor_t){0};
    queue->not_empty_monitor = (monitor_t){0};
    queue->finished_monitor  = (monitor_t){0};

    // Circular buffer initialization
    queue->capacity       = capacity;
    queue->count          = 0;
    queue->head           = 0;
    queue->tail           = 0;
    queue->finished       = 0;
    queue->is_initialized = 0;   // flipped at the very end on success

    if ((size_t)capacity > SIZE_MAX / sizeof(char*)) {
        queue->capacity = 0;
        return "capacity is too large";
    }

    // Items array (zeroed)
    queue->items = (char**)calloc((size_t)capacity, sizeof(char*));
    if (!queue->items) {
        queue->capacity = 0;
        return "calloc failed";
    }

    // Queue mutex
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue->items);
        queue->items    = NULL;
        queue->capacity = 0;
        return "pthread_mutex_init(lock) failed";
    }

    // Monitors, with rollback on failure
    if (monitor_init(&queue->not_full_monitor) != 0) {
        pthread_mutex_destroy(&queue->lock);
        free(queue->items); queue->items = NULL; queue->capacity = 0;
        return "monitor_init(not_full) failed";
    }
    if (monitor_init(&queue->not_empty_monitor) != 0) {
        monitor_destroy(&queue->not_full_monitor);
        pthread_mutex_destroy(&queue->lock);
        free(queue->items); queue->items = NULL; queue->capacity = 0;
        return "monitor_init(not_empty) failed";
    }
    if (monitor_init(&queue->finished_monitor) != 0) {
        monitor_destroy(&queue->not_empty_monitor);
        monitor_destroy(&queue->not_full_monitor);
        pthread_mutex_destroy(&queue->lock);
        free(queue->items); queue->items = NULL; queue->capacity = 0;
        return "monitor_init(finished) failed";
    }

    queue->is_initialized = 1;
    queue->magic = CP_MAGIC;     // mark init (with magic)
    return NULL;
}

void consumer_producer_destroy(consumer_producer_t* queue) {
    // Validate input
    if (queue == NULL)          return;
    if (!queue->is_initialized) return;

    // Free any leftover items still owned by the queue
    if (queue->items) {
        for (int i = 0; i < queue->capacity; ++i) {
            if (queue->items[i]) {
                free(queue->items[i]);
                queue->items[i] = NULL;
            }
        }
        free(queue->items);
        queue->items = NULL;
    }

    monitor_destroy(&queue->finished_monitor);
    monitor_destroy(&queue->not_empty_monitor);
    monitor_destroy(&queue->not_full_monitor);
    pthread_mutex_destroy(&queue->lock);

    // Back to a benign state
    queue->capacity  = 0;
    queue->count     = 0;
    queue->head      = 0;
    queue->tail      = 0;
    queue->finished  = 0;

    // clear monitor flags (their internals were already destroyed)
    queue->not_full_monitor.is_initialized  = 0;
    queue->not_full_monitor.signaled        = 0;
    queue->not_empty_monitor.is_initialized = 0;
    queue->not_empty_monitor.signaled       = 0;
    queue->finished_monitor.is_initialized  = 0;
    queue->finished_monitor.signaled        = 0;

    queue->is_initialized = 0;
    queue->magic = 0;  
}

const char* consumer_producer_put(consumer_producer_t* queue, const char* item) {
    if (queue == NULL)      return "queue is NULL";
    if (!queue->is_initialized) return "queue is not initialized";
    if (item == NULL)           return "item is NULL";

    pthread_mutex_lock(&queue->lock);

    // If the queue is already closed when we start, reject the put.
    // A put that *started before* finish is allowed to complete.
    if (queue->finished) {
        pthread_mutex_unlock(&queue->lock);
        return "queue finished";
    }

    // Block while full, release the lock while waiting
    while (queue->count == queue->capacity) {
        pthread_mutex_unlock(&queue->lock);
        (void)monitor_wait(&queue->not_full_monitor);
        pthread_mutex_lock(&queue->lock);
    }

    int was_empty = (queue->count == 0);
    queue->items[queue->tail] = (char*)item;     
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    if (was_empty) {
        monitor_signal(&queue->not_empty_monitor);
    }

    if (queue->count == queue->capacity) {
        monitor_reset(&queue->not_full_monitor);
    }

    pthread_mutex_unlock(&queue->lock);
    return NULL;
}

char* consumer_producer_get(consumer_producer_t* queue) {
    if (queue == NULL)  return NULL;
    if (!queue->is_initialized) return NULL;

    pthread_mutex_lock(&queue->lock);

    while (queue->count == 0 && !queue->finished) {
        pthread_mutex_unlock(&queue->lock);
        (void)monitor_wait(&queue->not_empty_monitor);
        pthread_mutex_lock(&queue->lock);
    }

    if (queue->count == 0 && queue->finished) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    int was_full = (queue->count == queue->capacity);
    char* item = queue->items[queue->head];
    queue->items[queue->head] = NULL;  
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    
    if (was_full) {
        monitor_signal(&queue->not_full_monitor);
    }
    
    if (queue->count == 0) {
        monitor_reset(&queue->not_empty_monitor);
        if (queue->finished) {
            monitor_signal(&queue->finished_monitor);
        }
    }

    pthread_mutex_unlock(&queue->lock);
    return item;
}

void consumer_producer_signal_finished(consumer_producer_t* queue) {
    // validate input
    if (queue == NULL) return;
    if (!queue->is_initialized) return;

    pthread_mutex_lock(&queue->lock); 
    if (!queue->finished) {
        queue->finished = 1; 

        monitor_signal(&queue->not_empty_monitor);// consumers blocked on empty
        monitor_signal(&queue->not_full_monitor); // producers blocked on full

        if (queue->count == 0) { 
            monitor_signal(&queue->finished_monitor); 
        }
    }
    pthread_mutex_unlock(&queue->lock);
}

int consumer_producer_wait_finished(consumer_producer_t* queue) {
    // validate input
    if (queue == NULL) {
        return -1;
    }         
    if (!queue->is_initialized){
        return -1;
    }
    (void)monitor_wait(&queue->finished_monitor);
    return 0;
}
