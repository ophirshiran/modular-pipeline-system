#include "plugin_common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h> // usleep

static int is_end_token(const char* s) {
    return s && strcmp(s, "<END>") == 0;
}

const char* plugin_transform(const char* input) {
    if (!input || is_end_token(input)) return input;

    const char* prefix = "[typewriter] ";

    for (const char* p = prefix; *p; ++p) {
        fputc(*p, stdout);
        fflush(stdout);
        usleep(100000); // 100ms 
    }
    for (const char* p = input; *p; ++p) {
        fputc(*p, stdout);
        fflush(stdout);
        usleep(100000); // 100ms 
    }
    fputc('\n', stdout);
    fflush(stdout);

    return input; 
}

const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "typewriter", queue_size);
}
