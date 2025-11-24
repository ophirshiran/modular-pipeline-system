#include "plugin_common.h"
#include <string.h>
#include <stdlib.h>

// Insert one space between every adjacent pair
// Passthrough for NULL, "<END>", empty string, and single char
const char* plugin_transform(const char* input) {
    if (!input) return NULL;

    // Do not touch the sentinel
    if (strcmp(input, "<END>") == 0) {
        return input; // passthrough
    }

    size_t n = strlen(input);

    // Empty or single char: no op, same pointer
    if (n <= 1) {
        return input; // passthrough
    }

    // For n chars, need n + (n-1) spaces = 2n-1 (+1 for NUL)
    size_t outn = 2 * n - 1;
    char* out = (char*)malloc(outn + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        out[j++] = input[i];
        if (i + 1 < n) out[j++] = ' ';
    }
    out[j] = '\0';
    return out;
}

const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "expander", queue_size);
}
