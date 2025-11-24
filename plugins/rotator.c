#include "plugin_common.h"
#include <string.h>
#include <stdlib.h>

// Right rotate by one: last char moves to the front.
// Passthrough for NULL, "<END>", empty string, and single char.
const char* plugin_transform(const char* input) {
    if (!input) return NULL;

    // Do not touch the sentinel
    if (strcmp(input, "<END>") == 0) {
        return input; // passthrough
    }

    size_t n = strlen(input);

    // Empty or single-char: no-op, same pointer
    if (n <= 1) {
        return input; // passthrough
    }

    // n >= 2: allocate and rotate right by one
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;

    out[0] = input[n - 1];
    memcpy(out + 1, input, n - 1);
    out[n] = '\0';
    return out;
}

const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "rotator", queue_size);
}
