#include "plugin_common.h"
#include <stdio.h>
#include <string.h>


/* exact END token check */
static int is_end_token(const char* s) {
    return s && strcmp(s, "<END>") == 0;
}

const char* plugin_transform(const char* input) {
    if (!input || is_end_token(input)) {
        // NULL: passthrough NULL; "<END>": passthrough same pointer, no output
        return input;
    }

    // // Print with the required prefix and a newline, even for empty strings
    // plugin_print_lock();
    fputs("[logger] ", stdout);
    fputs(input, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    // plugin_print_unlock();

    // Return the same pointer (no allocation here)
    return input;
}

const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "logger", queue_size);
}