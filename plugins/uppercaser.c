#include "plugin_common.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>




const char* plugin_transform(const char* input) {
    if (!input) return NULL;

    // Do not touch the sentinel
    if (strcmp(input, "<END>") == 0) {
        return input; // passthrough
    }

    // Empty string is a no-op and must return the same pointer
    size_t n = strlen(input);
    if (n == 0) {
        return input; // passthrough for ""
    }

    // For any non empty string, return a newly allocated uppercase copy
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; ++i) {
        out[i] = (char)toupper((unsigned char)input[i]);
    }
    out[n] = '\0';
    return out;
}

const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "uppercaser", queue_size);
}
