#!/usr/bin/env bash

set -euo pipefail

GREEN="\033[0;32m"; YELLOW="\033[1;33m"; RED="\033[0;31m"; NC="\033[0m"
ok()  { echo -e "${GREEN}[*]${NC} $*"; }
warn(){ echo -e "${YELLOW}[!]${NC} $*" >&2; }
err() { echo -e "${RED}[x]${NC} $*" >&2; }

# ensure-  project root 
[ -f "main.c" ] || { err "main.c not found. Run from project root."; exit 1; }
[ -d "plugins" ] || { err "plugins/ directory not found. Run from project root."; exit 1; }
[ -f "plugins/plugin_common.c" ] || { err "plugins/plugin_common.c not found."; exit 1; }
[ -d "plugins/sync" ] || { err "plugins/sync/ not found."; exit 1; }
[ -f "plugins/sync/monitor.c" ] || { err "plugins/sync/monitor.c not found."; exit 1; }
[ -f "plugins/sync/consumer_producer.c" ] || { err "plugins/sync/consumer_producer.c not found."; exit 1; }

OUT="output"
mkdir -p "$OUT"

CC=${CC:-gcc}

CFLAGS_MAIN="-Wall -Wextra -O2 -Iplugins -Iplugins/sync"
LDFLAGS_MAIN="-ldl"

PLUGIN_LIST=(logger uppercaser rotator flipper expander typewriter)

ok "Compiling analyzer"
$CC $CFLAGS_MAIN \
  main.c plugin_loader.c plugin_runtime.c \
  -o "$OUT/analyzer" \
  $LDFLAGS_MAIN
ok "Analyzer ready at $OUT/analyzer"

for plugin_name in "${PLUGIN_LIST[@]}"; do
  ok "Building plugin: $plugin_name"

  gcc -fPIC -shared -o "output/${plugin_name}.so" \
    "plugins/${plugin_name}.c" \
    "plugins/plugin_common.c" \
    "plugins/sync/monitor.c" \
    "plugins/sync/consumer_producer.c" \
    -ldl -lpthread
done

echo -e "${GREEN}✔ Build finished successfully.${NC}"
