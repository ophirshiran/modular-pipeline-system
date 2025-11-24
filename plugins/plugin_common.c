#include "plugin_common.h"
#include <stdio.h> // printf/fprintf
#include <string.h> // string manipulation functions
#include <stdlib.h> // malloc/free


static const char* k_default_plugin_name = "unknown";

// single instance per plugin
static plugin_context_t g_context_instance; // global plugin context instance, (zero initialized)

// one global print mutex per .so to keep stdout messages safe
//static pthread_mutex_t g_print_mutex = PTHREAD_MUTEX_INITIALIZER; 


void* plugin_consumer_thread(void* arg) {
    plugin_context_t* ctx = (plugin_context_t*)arg;

    for (;;) {
        char* s = consumer_producer_get(ctx->queue);
        if (!s) break; // finished+empty

        if (strcmp(s, "<END>") == 0) {
            free(s);

            
            const char* (*next_fn)(const char*) = NULL;
            pthread_mutex_lock(&ctx->lock_state);
            if (!ctx->end_pushed) {
                ctx->end_pushed = 1;
                next_fn = ctx->next_place_work;
            }
            pthread_mutex_unlock(&ctx->lock_state);

            if (next_fn) (void)next_fn("<END>");

            // Signal that the consumer is finished
            consumer_producer_signal_finished(ctx->queue);
            break;
        }

        // Transform
        const char* out = ctx->process_function ? ctx->process_function(s) : s;
        if (ctx->process_function && out == NULL) log_error(ctx, "transform failed");


        // Get the next function under lock to avoid race conditions with attach
        const char* (*next_fn)(const char*) = NULL;
        pthread_mutex_lock(&ctx->lock_state);
        next_fn = ctx->next_place_work;
        pthread_mutex_unlock(&ctx->lock_state);

        if (next_fn && out) { // if there is a next function and output is valid- transform it
            const char* nerr = next_fn(out);
            if (nerr) log_error(ctx, nerr);
        }


        // memory management, s is always ours (from the queue) ,always free at the end of the round
        // out: assuming the transform allocates new when it changes, can free after sending/if no next
        if (out != s) {
            free(s);
            if (out) free((void*)out);
        } else {
            free(s);
        }
    }

    
    pthread_mutex_lock(&ctx->lock_state);
    ctx->finished = 1;
    pthread_mutex_unlock(&ctx->lock_state);
    monitor_signal(&ctx->finished_monitor);
    return NULL;
}

const char* plugin_get_name(void) {
    plugin_context_t* ctx = &g_context_instance;
    if (!ctx || !ctx->initialized || !ctx->name || ctx->name[0] == '\0') {
        return k_default_plugin_name; // "unknown"
    }
    return ctx->name;
}
void log_error(plugin_context_t* context, const char* message) {
    const char* name = (context && context->name) ? context->name : plugin_get_name();
    const char* msg  = message ? message : "(null)";

    fprintf(stderr, "[ERROR][%s] - %s\n", name, msg);
    fflush(stderr);
  

}

void log_info(plugin_context_t* context, const char* message) {
    const char* name = (context && context->name) ? context->name : plugin_get_name();
    const char* msg  = message ? message : "(null)";

   
    fprintf(stderr, "[INFO][%s] - %s\n", name, msg);
    fflush(stderr);
  
}

const char* common_plugin_init(const char* (*process_function)(const char*), const char* name, int queue_size) {
    // 1) Basic validation
    if (process_function == NULL) return "process_function is NULL";
    if (queue_size <= 0)          return "invalid queue_size";
    if (g_context_instance.initialized) return "already initialized";
    if (name == NULL || strcmp(name, "") == 0) return "name is invalid";

    // 2) Put the context in a known state (before any allocations)
    g_context_instance.name            = name ? name : k_default_plugin_name;
    g_context_instance.process_function= process_function;
    g_context_instance.next_place_work = NULL;

    g_context_instance.finished        = 0;
    g_context_instance.thread_created  = 0;
    g_context_instance.thread_joined   = 0;
    g_context_instance.end_pushed      = 0;
    g_context_instance.queue           = NULL; // set after successful init

    // 3) Init common synchronization
    if (pthread_mutex_init(&g_context_instance.lock_state, NULL) != 0) {
        return "lock_state init failed";
    }
    if (monitor_init(&g_context_instance.finished_monitor) != 0) {
        pthread_mutex_destroy(&g_context_instance.lock_state);
        return "finished_monitor init failed";
    }

    // 4) Build the input queue
    consumer_producer_t* q = (consumer_producer_t*)malloc(sizeof(*q));
    if (!q) {
        monitor_destroy(&g_context_instance.finished_monitor);
        pthread_mutex_destroy(&g_context_instance.lock_state);
        return "queue alloc failed";
    }
    const char* qerr = consumer_producer_init(q, queue_size);
    if (qerr != NULL) {
        free(q);
        monitor_destroy(&g_context_instance.finished_monitor);
        pthread_mutex_destroy(&g_context_instance.lock_state);
        return "queue init failed";
    }
    g_context_instance.queue = q;

    // 5) Launch the consumer thread for this plugin
    int rc = pthread_create(&g_context_instance.consumer_thread,
                            NULL,
                            plugin_consumer_thread,
                            &g_context_instance);
    if (rc != 0) {
        consumer_producer_destroy(q);
        free(q);
        g_context_instance.queue = NULL;
        monitor_destroy(&g_context_instance.finished_monitor);
        pthread_mutex_destroy(&g_context_instance.lock_state);
        return "pthread_create failed";
    }
    g_context_instance.thread_created = 1;

    // 6) Mark as initialized on success
    g_context_instance.initialized = 1;
    return NULL; // success
}

// Small internal helper: duplicate a C string without using strdup 
static char* dup_cstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);  // includes the '\0'
    return p;
}


const char* plugin_fini(void) {
    plugin_context_t* ctx = &g_context_instance;
    if (!ctx->initialized) return "plugin not initialized";

    if (ctx->thread_created && !ctx->thread_joined) {
        consumer_producer_signal_finished(ctx->queue);
        (void)pthread_join(ctx->consumer_thread, NULL);
        ctx->thread_joined = 1;
    }

    if (ctx->queue) {
        consumer_producer_destroy(ctx->queue);
        free(ctx->queue);
        ctx->queue = NULL;
    }

    monitor_destroy(&ctx->finished_monitor);
    pthread_mutex_destroy(&ctx->lock_state);

    
    ctx->initialized     = 0;
    ctx->finished        = 0;
    ctx->thread_created  = 0;
    ctx->end_pushed      = 0;
    ctx->thread_joined   = 0;
    ctx->next_place_work = NULL;
    ctx->process_function= NULL;
    

    return NULL;
}


const char* plugin_place_work(const char* str) {

    plugin_context_t* ctx = &g_context_instance;

    // Validate basic state
    if (!ctx->initialized)
        return "plugin not initialized";
    if (str == NULL)
        return "string is NULL";

    // Copy the string so ownership is clear and safe
    char* copy = dup_cstr(str);
    if (!copy)
        return "alloc failed";

    // Push into the bounded queue (blocks if full, as required)
    const char* qerr = consumer_producer_put(ctx->queue, copy);
    if (qerr != NULL) {
        // On failure, caller keeps ownership; free our copy to avoid a leak
        free(copy);
        return qerr;  // propagate the queue error (e.g., "queue finished")
    }

    return NULL;  // success
    
}

void plugin_attach(const char* (*next_place_work)(const char*)) {
    plugin_context_t* ctx = &g_context_instance;

    pthread_mutex_lock(&ctx->lock_state);

    if (!ctx->initialized) {
        log_error(ctx, "attach called before init");
        goto out;
    }

    
    if (ctx->finished || (ctx->queue && ctx->queue->finished)) {
        log_error(ctx, "attach after finish is not allowed");
        goto out;
    }

   
    if (ctx->next_place_work != NULL) {
        if (ctx->next_place_work == next_place_work) {     
        } else {
            log_error(ctx, "attach called twice with a different target; keeping existing wiring");
        }
        goto out;
    }

    ctx->next_place_work = next_place_work;

out:
    pthread_mutex_unlock(&ctx->lock_state);
}

const char* plugin_wait_finished(void) {
    plugin_context_t* ctx = &g_context_instance;
    if (!ctx->initialized) return "plugin not initialized";
    if (monitor_wait(&ctx->finished_monitor) != 0) return "wait failed";
    return NULL;
}
