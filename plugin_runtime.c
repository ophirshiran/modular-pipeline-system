#include "plugin_runtime.h"
#include <stdlib.h>
#include <string.h>

// Small strdup to avoid non-portable prototypes
static char* dup_cstr(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

int init_all_plugins(plugin_handle_t* arr, size_t count, int queue_size,
                     size_t* failed_index, char** failed_msg) {
    if (failed_index) *failed_index = (size_t)-1;
    if (failed_msg)   *failed_msg   = NULL;
    if (!arr || count == 0) return 0; // nothing to do

    for (size_t i = 0; i < count; ++i) {
        const char* err = arr[i].init ? arr[i].init(queue_size) : "missing init()";
        if (err != NULL) {
            if (failed_index) *failed_index = i;
            if (failed_msg)   *failed_msg   = dup_cstr(err ? err : "init failed");
            // rollback already initialized plugins [0, i)
            for (size_t j = 0; j < i; ++j) {
                if (arr[j].fini) (void)arr[j].fini();
            }
            return -1;
        }
    }
    return 0;
}

void fini_prefix(plugin_handle_t* arr, size_t upto) {
    if (!arr) return;
    for (size_t i = 0; i < upto; ++i) {
        if (arr[i].fini) (void)arr[i].fini();
    }
}



int attach_chain(plugin_handle_t* arr, size_t count,
                 size_t* failed_index, char** failed_msg) {
    if (failed_index) *failed_index = (size_t)-1;
    if (failed_msg)   *failed_msg   = NULL;

    if (!arr || count == 0) return 0;     // nothing to do
    if (count == 1)         return 0;     // single plugin: last in chain

    for (size_t i = 0; i + 1 < count; ++i) {
        if (!arr[i].attach) {
            if (failed_index) *failed_index = i;
            if (failed_msg)   *failed_msg = dup_cstr("missing attach()");
            return -1;
        }
        if (!arr[i + 1].place_work) {
            if (failed_index) *failed_index = i;
            if (failed_msg)   *failed_msg = dup_cstr("missing next place_work()");
            return -1;
        }
        // connect i => i+1
        arr[i].attach(arr[i + 1].place_work);
    }
    return 0;
}