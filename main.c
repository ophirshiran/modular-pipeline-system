#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>


#include "plugin_loader.h"   // load_all_plugins / unload_all_plugins
#include "plugin_runtime.h"  // init_all_plugins / attach_chain / fini_prefix

// usage printing 'as required 
static void print_usage(FILE* out) {
    fprintf(out, "Usage: ./analyzer <queue_size> <plugin1> <plugin2> ... <pluginN>\n");
    fprintf(out, "\n");
    fprintf(out, "Arguments:\n");
    fprintf(out, "  queue_size    Maximum number of items in each plugin's queue\n");
    fprintf(out, "  plugin1..N    Names of plugins to load (without .so extension)\n");
    fprintf(out, "\n");
    fprintf(out, "Available plugins:\n");
    fprintf(out, "  logger        - Logs all strings that pass through\n");
    fprintf(out, "  typewriter    - Simulates typewriter effect with delays\n");
    fprintf(out, "  uppercaser    - Converts strings to uppercase\n");
    fprintf(out, "  rotator       - Move every character to the right.  Last character moves to\n");
    fprintf(out, "the beginning.\n");
    fprintf(out, "  flipper       - Reverses the order of characters\n");
    fprintf(out, "  expander      - Expands each character with spaces\n");
    fprintf(out, "\n");
    fprintf(out, "Example:\n");
    fprintf(out, "  ./analyzer 20 uppercaser rotator logger\n");
    fprintf(out, "  echo 'hello' | ./analyzer 20 uppercaser rotator logger\n");
    fprintf(out, "  echo '<END>' | ./analyzer 20 uppercaser rotator logger\n");
}

// helpers (step1)
static int parse_positive_int(const char* s, int* out) {
    if (!s || !*s) return 0;
    errno = 0;
    char* endp = NULL;
    long v = strtol(s, &endp, 10);
    if (endp == s) return 0;
    if (*endp != '\0') return 0;
    if ((errno == ERANGE) || v > INT_MAX || v < INT_MIN) return 0;
    if (v <= 0) return 0;
    *out = (int)v;
    return 1;
}
static int is_valid_plugin_arg(const char* s) {
    return s && s[0] != '\0';
}

#define MAX_LINE 1024

int main(int argc, char** argv) {
    // validation 
    if (argc < 3) {
        fprintf(stderr, "error: missing arguments\n");
        print_usage(stdout);
        return 1;
    }
    int queue_size = 0;
    if (!parse_positive_int(argv[1], &queue_size)) {
        fprintf(stderr, "error: invalid queue size '%s'\n", argv[1] ? argv[1] : "");
        print_usage(stdout);
        return 1;
    }
    const int n_plugins = argc - 2;
    for (int i = 0; i < n_plugins; ++i) {
        if (!is_valid_plugin_arg(argv[2 + i])) {
            fprintf(stderr, "error: invalid plugin name at position %d\n", i + 1);
            print_usage(stdout);
            return 1;
        }
    }

    // load plugin .so files 
    plugin_handle_t* plugs = NULL;
    char* load_err = NULL;
    if (load_all_plugins((const char* const*)(argv + 2), (size_t)n_plugins,
                         &plugs, &load_err) != 0) {
        fprintf(stderr, "error: %s\n", load_err ? load_err : "failed to load plugins");
        free(load_err);
        print_usage(stdout);
        return 1;
    }

    // init(queue_size) for each plugin 
    size_t init_failed_idx = (size_t)-1;
    char* init_err = NULL;
    if (init_all_plugins(plugs, (size_t)n_plugins, queue_size,
                         &init_failed_idx, &init_err) != 0) {
        const char* pname = (init_failed_idx < (size_t)n_plugins && plugs[init_failed_idx].name)
                          ? plugs[init_failed_idx].name : "(unknown)";
        fprintf(stderr, "error: plugin '%s' init failed: %s\n",
                pname, init_err ? init_err : "init failed");
        free(init_err);
        
        unload_all_plugins(plugs, (size_t)n_plugins);
        return 2; // error code
    }

    // attach 
    size_t attach_failed_idx = (size_t)-1;
    char* attach_err = NULL;
    if (attach_chain(plugs, (size_t)n_plugins, &attach_failed_idx, &attach_err) != 0) {
        const char* src = (attach_failed_idx < (size_t)n_plugins && plugs[attach_failed_idx].name)
                        ? plugs[attach_failed_idx].name : "(unknown)";
        const char* dst = (attach_failed_idx + 1 < (size_t)n_plugins && plugs[attach_failed_idx + 1].name)
                        ? plugs[attach_failed_idx + 1].name : "(unknown)";
        fprintf(stderr, "error: attach failed for '%s' -> '%s': %s\n",
                src, dst, attach_err ? attach_err : "attach failed");
        free(attach_err);
        // roll back everything that was initialized
        fini_prefix(plugs, (size_t)n_plugins);
        unload_all_plugins(plugs, (size_t)n_plugins);
        return 3; // Step 4 failure code
    }

    // read input from STDIN, strip '\n', feed first plugin
    {
    char line[MAX_LINE + 2]; // +1 for '\n', +1 for '\0'
    int seen_end = 0;

    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) {
            // No auto-END injection on EOF per instructor.
            // Avoid busy-spin: if EOF, clear and sleep briefly.
            if (feof(stdin)) {
                clearerr(stdin);
                usleep(50 * 1000);
                continue; // keep waiting for more input
            }
            if (ferror(stdin)) {
                fprintf(stderr, "error: stdin read failed\n");
                // You may choose to break or continue. We'll continue waiting.
                clearerr(stdin);
                continue;
            }
        } else {
            // Strip trailing newline(s)
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') {
                line[--len] = '\0';
                if (len > 0 && line[len - 1] == '\r') {
                    line[--len] = '\0';
                }
            }

            if (strcmp(line, "<END>") == 0) {
                const char* perr = plugs[0].place_work("<END>");
                if (perr) fprintf(stderr, "error: place_work(<END>) failed: %s\n", perr);
                seen_end = 1;
                break; 
            }

            const char* perr = plugs[0].place_work(line);
            if (perr) {
                fprintf(stderr, "error: place_work failed: %s\n", perr);
            }
        }
    }

    (void)seen_end; // only if seen_end==1
}

    // wait for all plugins to finish 
    for (int i = 0; i < n_plugins; ++i) {
        if (plugs[i].wait_finished) {
            const char* werr = plugs[i].wait_finished();
            if (werr) {
                fprintf(stderr, "error: wait_finished('%s'): %s\n",
                        plugs[i].name ? plugs[i].name : "(unknown)", werr);
            }
        }
    }

    // cleanup (unload all plugins)
    fini_prefix(plugs, (size_t)n_plugins);           // call plugin_fini() for each
    unload_all_plugins(plugs, (size_t)n_plugins);    // dlclose() +free handles

    // finishhhhh :)
    printf("Pipeline shutdown complete\n");
    return 0;
}
