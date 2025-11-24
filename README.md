# Modular Pipeline System

Multithreaded, plugin based string processing pipeline in C.

## Overview

This project implements a configurable pipeline of string processing plugins.  
Each plugin runs in its own thread and communicates with the next stage through a bounded, thread safe queue.

The program:

- Reads lines from standard input.
- Sends each line through a chain of plugins in the order given on the command line.
- Lets each plugin transform the string or produce side effects such as logging.
- Uses a special control line `<END>` that flows through the entire pipeline and then triggers a clean shutdown.

The design focuses on clear separation of concerns, safe concurrency and easy extensibility.

## Features

- Dynamic loading of plugins from shared objects (`.so`) using `dlopen`.
- One worker thread per plugin.
- Thread safe bounded producer consumer queues between plugins.
- Clean shutdown semantics using a dedicated sentinel value (`<END>`).
- Simple command line interface that defines the pipeline structure.
- Minimal, dependency free C implementation that uses only the standard library plus `pthread` and `dl`.

## Project layout

The exact file names may vary slightly, but the structure is conceptually:

- `main.c`  
  Entry point of the application. Parses command line arguments, loads plugins, builds the pipeline, reads input from `stdin` and coordinates shutdown.

- `monitor.c`, `monitor.h`  
  Monitor abstraction wrapping a mutex and condition variable. Provides a clean API for waiting and signaling.

- `consumer_producer.c`, `consumer_producer.h`  
  Bounded, thread safe queue implementation used to connect plugins. Supports normal operation and a finished state for graceful shutdown.

- `plugin_common.c`, `plugin_common.h`  
  Shared plugin infrastructure and SDK helpers. Handles plugin initialization, error reporting and passing the `<END>` sentinel through exactly once.

- `plugins/logger.c`  
  Logging plugin that prints each string with a prefix.

- `plugins/typewriter.c`  
  Plugin that prints strings character by character with a small delay.

- `plugins/uppercaser.c`  
  Plugin that converts alphabetic characters to upper case.

- `plugins/rotator.c`  
  Plugin that rotates all characters in the string one position to the right.

- `plugins/flipper.c`  
  Plugin that reverses the string.

- `plugins/expander.c`  
  Plugin that inserts a space between every pair of characters.

- `build.sh`  
  Convenience script that compiles the main executable and all plugins into the `output/` directory.

- `test.sh`  
  Script that builds the project and runs a collection of automated tests.

- `README`  
  Optional minimal text file with personal details for formal or automated environments that require it.

- `README.md`  
  This documentation file.

## Building

The project is written in C and uses the C standard library, `pthread` and `dl`.

From the project root:

```bash
chmod +x build.sh
./build.sh
```

The script performs basic checks and then compiles:

- The main analyzer executable into `output/analyzer`.
- All plugins into `output/*.so`.

If the build fails, fix any compilation errors or warnings and run the script again.

## Usage

After a successful build, the main binary is located at `output/analyzer`.

Command line syntax:

```bash
./output/analyzer <queue_size> <plugin1> <plugin2> ... <pluginN>
```

- `queue_size` must be a positive integer. The same capacity is used for all queues between plugins.
- At least one plugin name is required.
- Plugin names correspond to existing shared objects. For example the name `logger` expects a file such as `output/logger.so`.

### Simple example

Uppercase then log:

```bash
echo "hello modular pipeline" | ./output/analyzer 10 uppercaser logger
```

Example output:

```text
[logger] HELLO MODULAR PIPELINE
```

### More complex pipeline

Demonstrating several plugins in sequence:

```bash
echo -e "pipeline demo\nsecond line\n<END>" | ./output/analyzer 20 uppercaser rotator logger typewriter
```

The string passes through four plugins in sequence:

1. `uppercaser` converts the text to upper case.
2. `rotator` moves the last character to the front.
3. `logger` prints the final string with a prefix.
4. `typewriter` prints the same string slowly, character by character.

### Interactive example

The analyzer can also be used interactively.

User session:

```bash
$ ./output/analyzer 20 uppercaser rotator logger typewriter
pipeline demo
second line
<END>
```

Explanation:

- The first line is the command that starts the analyzer with a queue size of 20 and four plugins.
- The next lines are user input. Each line is processed by all plugins in order.
- When the user types `<END>` and presses enter, the sentinel value is sent through the pipeline and triggers a clean shutdown.
- After `<END>` is processed, the program terminates and control returns to the shell.

## Plugin pipeline

At runtime the program builds a pipeline of independent plugin stages.  
Each stage runs in its own thread and communicates only via bounded queues.

General view:

```text
┌─────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌─────────────┐
│   Input     │──▶ │   Plugin 1   │──▶ │   Plugin 2   │──▶ │   Plugin 3   │──▶ │   Output    │
│ stdin lines │    │ transform    │    │ transform    │    │ transform    │    │ to stdout   │
└─────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └─────────────┘

            ┌─────────┐      ┌─────────┐      ┌─────────┐      ┌─────────┐
            │ queue 0 │      │ queue 1 │      │ queue 2 │      │ queue 3 │
            └─────────┘      └─────────┘      └─────────┘      └─────────┘
```

Each plugin pulls strings from its input queue, processes them and pushes the result
to the next queue in the chain.

### Example pipeline and data flow

The same idea with concrete plugins:

```text
┌─────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌───────────────┐
│   Input     │──▶ │  uppercaser  │──▶ │   rotator    │──▶ │    logger    │──▶ │   typewriter  │
│ "pipeline"  │    │ to UPPERCASE │    │ rotate chars │    │ log result   │    │ slow printing │
└─────────────┘    └──────────────┘    └──────────────┘    └──────────────┘    └───────────────┘
```

Conceptual data flow:

- Input line:  
  `pipeline demo`
- After `uppercaser`:  
  `PIPELINE DEMO`
- After `rotator` (last character moved to the front):  
  `OPIPELINE DEM`
- After `logger` (written to stdout with a prefix):  
  `[logger] OPIPELINE DEM`
- After `typewriter` (same string, printed character by character with a delay):  
  `[typewriter] OPIPELINE DEM` (appears gradually in the terminal).

Special control line:

- When the line `<END>` is read from standard input it flows through the same pipeline.
- Plugins treat `<END>` as a sentinel value and do not transform or print it.
- After `<END>` reaches the last plugin all worker threads shut down cleanly and the program exits.

## Demo video

https://github.com/user-attachments/assets/769595ac-6cde-4bac-b20e-f417bbb1760e

The video illustrates the following scenarios in chronological order:

1. **Build and setup**  
   The project is built using `./build.sh`, producing the `output/analyzer` binary and the plugin shared objects in `output/*.so`.

2. **Simple pipeline run**  
   A short command line example demonstrates a basic pipeline with a small number of plugins, showing how input text flows through the system and is transformed before being printed.

3. **Multi plugin pipeline**  
   A longer example shows several plugins chained together, including text transformation and logging. The video highlights how the same input string is gradually transformed as it moves through the pipeline.

4. **Interactive usage and shutdown**  
   The analyzer is started and the user types several lines directly into the terminal. The special `<END>` line is then entered and the video shows how this sentinel value triggers a clean shutdown and returns control to the shell.

## Built in plugins

Summary of the included plugins:

- `logger`  
  Prints each string with a prefix and a newline.

- `typewriter`  
  Prints each string with a prefix, then prints each character with a short delay.

- `uppercaser`  
  Allocates a new string where all alphabetic characters are converted to upper case.

- `rotator`  
  Allocates a new string where the last character is moved to the front and all other characters are shifted right by one.

- `flipper`  
  Allocates a new string with all characters in reverse order.

- `expander`  
  Allocates a new string that inserts a single space between every pair of characters.

All plugins share the same basic contract:

- `plugin_init` is called once when the pipeline is created.
- `plugin_transform` is called for every string, including the `<END>` sentinel.
- `plugin_fini` is called during shutdown so the plugin can release resources.

## Testing

To run the automated tests:

```bash
chmod +x test.sh
./test.sh
```

The script usually:

1. Verifies that it is running from the project root.
2. Calls `build.sh`.
3. Runs a collection of success and failure scenarios.
4. Checks both standard output and standard error to make sure messages and error handling behave as expected.

If any test fails the script exits with a non zero status code.

## Extending the system

Adding a new plugin typically involves:

1. Creating a new source file in the `plugins/` folder that implements the required plugin interface.
2. Including the shared `plugin_common.h` header.
3. Implementing the transformation logic in `plugin_transform`.
4. Updating the build script or build system so that the new plugin is compiled into a shared object.

Once compiled, the new plugin can be used by passing its name on the command line as another stage in the pipeline.

This document should be enough for a new user or reviewer to understand, build and run the modular pipeline system.
