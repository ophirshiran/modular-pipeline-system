#ifndef PLUGIN_RUNTIME_H
#define PLUGIN_RUNTIME_H

#include <stddef.h>
#include "plugin_loader.h"


// init plugins from left to right
// queue_size: size for per-plugin queues (if used)
// On success: return 0
// On failure: return -1, set *failed_index to the plugin that failed,
// and *failed_msg to a heap-allocated error string (caller frees)
int init_all_plugins(plugin_handle_t* arr, size_t count, int queue_size,
                     size_t* failed_index, char** failed_msg);

// Finalize [0, upto) plugins (ignores errors)
void fini_prefix(plugin_handle_t* arr, size_t upto);

// connect plugins into a chain: plugins[i] => plugins[i+1].place_work
// On success: return 0
// On failure: return -1, set *failed_index to i (the source),
// and *failed_msg to a heap-allocated error string (caller frees)
int attach_chain(plugin_handle_t* arr, size_t count,
                 size_t* failed_index, char** failed_msg);

#endif // PLUGIN_RUNTIME_H
