#include "plugin_common.h"
#include <string.h>
#include <stdlib.h>

const char* plugin_transform(const char* input) {
    if (!input) return NULL;

    // Do not touch the sentinel
    if (strcmp(input, "<END>") == 0) {
        return input; // passthrough
    }

    // Empty string or single char are no-ops and must return the same pointer
    size_t n = strlen(input);
    if (n <= 1) {
        return input; // passthrough
    }

    // For length >= 2, return a newly allocated reversed copy
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; ++i) {
        out[i] = input[n - 1 - i];
    }
    out[n] = '\0';
    return out;
}

const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "flipper", queue_size);
}
