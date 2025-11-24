#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// function pointer signatures per the plugin SDK
typedef const char* (*plugin_init_func_t)(int queue_size);
typedef const char* (*plugin_fini_func_t)(void);
typedef const char* (*plugin_place_work_func_t)(const char* s);
typedef void        (*plugin_attach_func_t)(const char* (*next_place_work)(const char*));
typedef const char* (*plugin_wait_finished_func_t)(void);

// to check-----
typedef const char* (*plugin_get_name_func_t)(void);

// Handle (for each loaded plugin)
typedef struct {
    plugin_init_func_t          init; // plugin_init
    plugin_fini_func_t          fini; // plugin_fini
    plugin_place_work_func_t    place_work; // plugin_place_work
    plugin_attach_func_t        attach; //plugin_attach
    plugin_wait_finished_func_t wait_finished; //plugin_wait_finished
    char*                       name;   //  copy of argv name (without .so)
    void*                       handle; // dlopen handle (for .so.)
} plugin_handle_t;

// Load an array of plugins by name
int load_all_plugins(const char* const* names, size_t count,
                     plugin_handle_t** out, char** out_error);

// Unloads all plugins and frees plugin_handle_t array and names (safe on NULL).
void unload_all_plugins(plugin_handle_t* arr, size_t count);

// Returns 1 if a duplicate name exists (and writes indices to *i,*j), else 0.
int find_duplicate_name(const char* const* names, size_t count,
                        size_t* i, size_t* j);

#ifdef __cplusplus
}
#endif

#endif // PLUGIN_LOADER_H
