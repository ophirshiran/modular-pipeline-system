#include "plugin_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// Simple strdup replacement to avoid non standard prototypes 
static char* dup_cstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

// Decide a filesystem path for the plugin .so: try "output/<name>.so" then "./<name>.so".
static int build_candidate_paths(const char* name, char path1[], size_t cap1, char path2[], size_t cap2) {
    if (!name || !*name) return 0;
    int n1 = snprintf(path1, cap1, "output/%s.so", name);
    int n2 = snprintf(path2, cap2, "./%s.so", name);
    return (n1 > 0 && (size_t)n1 < cap1) && (n2 > 0 && (size_t)n2 < cap2);
}

// Try dlopen on a couple of candidate paths. Returns handle or NULL= writes dlerror to *err on failure.
static void* open_so_candidates(const char* name, char** err_out) {
    char p1[1024], p2[1024];
    if (!build_candidate_paths(name, p1, sizeof(p1), p2, sizeof(p2))) {
        *err_out = dup_cstr("invalid plugin name");
        return NULL;
    }

    void* h = dlopen(p1, RTLD_NOW | RTLD_LOCAL);
    if (h) return h;

    // Grab first error, try second path as fallback
    const char* e1 = dlerror();
    (void)e1; // kept for debugging if desired

    h = dlopen(p2, RTLD_NOW | RTLD_LOCAL);
    if (h) return h;

    const char* e2 = dlerror();
    // report the second error, it's the last failure reason
    size_t need = strlen("dlopen failed: ") + (e2 ? strlen(e2) : 0) + 1;
    char* msg = (char*)malloc(need);
    if (!msg) return NULL;
    snprintf(msg, need, "dlopen failed: %s", e2 ? e2 : "unknown error");
    *err_out = msg;
    return NULL;
}

// dlsym wrapper that returns NULL and writes an error string on failure.
static void* must_dlsym(void* handle, const char* sym, const char* plug_name, char** err_out) {
    dlerror(); // clear
    void* p = dlsym(handle, sym);
    const char* e = dlerror();
    if (e) {
        size_t need = strlen("dlsym('') failed for plugin '': ") + strlen(sym) + strlen(plug_name) + strlen(e) + 1;
        char* msg = (char*)malloc(need);
        if (!msg) return NULL;
        snprintf(msg, need, "dlsym('%s') failed for plugin '%s': %s", sym, plug_name, e);
        *err_out = msg;
        return NULL;
    }
    return p;
}

int find_duplicate_name(const char* const* names, size_t count, size_t* oi, size_t* oj) {
    for (size_t i = 0; i < count; ++i) {
        if (!names[i]) continue;
        for (size_t j = i + 1; j < count; ++j) {
            if (!names[j]) continue;
            if (strcmp(names[i], names[j]) == 0) {
                if (oi) *oi = i;
                if (oj) *oj = j;
                return 1;
            }
        }
    }
    return 0;
}

static void free_partial(plugin_handle_t* arr, size_t upto) {
    if (!arr) return;
    for (size_t i = 0; i < upto; ++i) {
        if (arr[i].handle) dlclose(arr[i].handle);
        free(arr[i].name);
    }
    free(arr);
}

int load_all_plugins(const char* const* names, size_t count,
                     plugin_handle_t** out, char** out_error) {
    *out = NULL;
    if (out_error) *out_error = NULL;

    // Enforce single appearance of each plugin (per your project policy)
    size_t di = 0, dj = 0;
    if (find_duplicate_name(names, count, &di, &dj)) {
        const char* a = names[di] ? names[di] : "";
        const char* b = names[dj] ? names[dj] : "";
        size_t need = strlen("duplicate plugin name: '' and ''") + strlen(a) + strlen(b) + 1;
        char* msg = (char*)malloc(need);
        if (msg) snprintf(msg, need, "duplicate plugin name: '%s' and '%s'", a, b);
        if (out_error) *out_error = msg;
        return -1;
    }

    plugin_handle_t* arr = (plugin_handle_t*)calloc(count, sizeof(*arr));
    if (!arr) {
        if (out_error) *out_error = dup_cstr("alloc failed for plugin handles");
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        const char* plug = names[i];
        char* err = NULL;

        // open .so
        void* h = open_so_candidates(plug, &err);
        if (!h) {
            if (!err) err = dup_cstr("dlopen failed");
            if (out_error) *out_error = err;
            free_partial(arr, i);
            return -1;
        }

        // resolve required symbols exactly as in the SDK
        plugin_init_func_t          init =
            (plugin_init_func_t)         must_dlsym(h, "plugin_init",          plug, &err);
        plugin_fini_func_t          fini =
            (plugin_fini_func_t)         must_dlsym(h, "plugin_fini",          plug, &err);
        plugin_place_work_func_t    place_work =
            (plugin_place_work_func_t)   must_dlsym(h, "plugin_place_work",    plug, &err);
        plugin_attach_func_t        attach =
            (plugin_attach_func_t)       must_dlsym(h, "plugin_attach",        plug, &err);
        plugin_wait_finished_func_t wait_finished =
            (plugin_wait_finished_func_t)must_dlsym(h, "plugin_wait_finished", plug, &err);

        if (err) {
            // some symbol missing
            dlclose(h);
            if (out_error) *out_error = err;
            free_partial(arr, i);
            return -1;
        }

        // store handle
        arr[i].init          = init;
        arr[i].fini          = fini;
        arr[i].place_work    = place_work;
        arr[i].attach        = attach;
        arr[i].wait_finished = wait_finished;
        arr[i].handle        = h;
        arr[i].name          = dup_cstr(plug);
        if (!arr[i].name) {
            if (out_error) *out_error = dup_cstr("alloc failed for plugin name");
            // cleanup current and previous
            dlclose(h);
            free_partial(arr, i);
            return -1;
        }
    }

    *out = arr;
    return 0;
}

void unload_all_plugins(plugin_handle_t* arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; ++i) {
        if (arr[i].handle) dlclose(arr[i].handle);
        free(arr[i].name);
    }
    free(arr);
}
