#!/usr/bin/env bash
set -euo pipefail

green() { printf "\033[0;32m[PASS]\033[0m %s\n" "$*"; }
red()   { printf "\033[0;31m[FAIL]\033[0m %s\n" "$*"; exit 1; }
note()  { printf "\033[1;33m[*]\033[0m %s\n" "$*"; }

must_be_root() { [[ -f build.sh && -x build.sh && -d plugins ]] || red "run from project root"; }

# run "name" "stdin" <cmd...>
run() {
  NAME="$1"; shift
  IN="$1";   shift
  o="$(mktemp)"; e="$(mktemp)"
  set +e
  eval "$* >\"$o\" 2>\"$e\"" <<<"$IN"
  RC=$?
  set -e
  OUT="$(cat "$o")"
  ERR="$(cat "$e")"
  rm -f "$o" "$e"
}

rc()   { [[ $RC -eq $1 ]] || red "$NAME: rc=$RC (want $1)"; }
haso() { grep -Fq "$1" <<<"$OUT" || red "$NAME: stdout missing: $1"; }
hase() { grep -Fq "$1" <<<"$ERR" || red "$NAME: stderr missing: $1"; }
o_empty(){ [[ -z "$OUT" ]] || red "$NAME: stdout not empty"; }
e_empty(){ [[ -z "$ERR" ]] || red "$NAME: stderr not empty"; }
last_is(){ local last; last="$(tail -n1 <<<"$OUT")"; [[ "$last" == "$1" ]] || red "$NAME: last='$last' (want '$1')"; }

main() {
  must_be_root
  note "build"
  ./build.sh >/dev/null
  [[ -x ./analyzer ]] || ln -sf output/analyzer analyzer
  [[ -x ./analyzer ]] || red "analyzer missing"
  A=./analyzer

  # ---- argument errors ----
  run "no args" "" "$A";                         rc 1; haso "Usage:"; hase "error:";            green "no args"
  run "only q" "" "$A 10";                       rc 1; haso "Usage:"; hase "error:";            green "only q"
  run "q=0"   "" "$A 0 logger";                  rc 1; haso "Usage:"; hase "invalid queue";      green "q=0"
  run "q<0"   "" "$A -3 logger";                 rc 1; haso "Usage:"; hase "invalid queue";      green "q<0"
  run "q bad" "" "$A 1x logger";                 rc 1; haso "Usage:"; hase "invalid queue";      green "q bad"
  run "empty plugin" "" "$A 8 ''";               rc 1; haso "Usage:"; hase "invalid plugin";     green "empty plugin"
  run "dup plugin" "" "$A 8 uppercaser uppercaser"; rc 1; haso "Usage:"; hase "duplicate";      green "dup plugin"

  # ---- load failures ----
  run "missing .so" "" "$A 8 notexist";          rc 1; haso "Usage:"; hase "dlopen";             green "missing .so"

  # ---- simple sinks ----
  run "logger multi" $'one\ntwo\n\n<END>\n' "$A 8 logger"
  rc 0; haso "[logger] one"; haso "[logger] two"; haso "[logger] "; last_is "Pipeline shutdown complete"; e_empty; green "logger multi"

  run "typewriter short" $'Hi all\n<END>\n' "$A 8 typewriter"
  rc 0; haso "[typewriter] Hi all"; last_is "Pipeline shutdown complete"; e_empty; green "typewriter short"

  # ---- empty payload (only <END>) ----
  run "empty payload" $'<END>\n' "$A 8 logger"
  rc 0
  # only the final line should be printed
  [[ -z "$(printf "%s" "$OUT" | sed '/^Pipeline shutdown complete$/d' | sed '/^[[:space:]]*$/d')" ]] || red "extra stdout (expected only final line)"
  last_is "Pipeline shutdown complete"
  e_empty
  green "empty payload"

  # ---- chains (unique plugins only) ----
  # upper → logger (baseline)
  run "upper→log" $'hello\n<END>\n' "$A 10 uppercaser logger"
  rc 0; haso "[logger] HELLO"; last_is "Pipeline shutdown complete"; e_empty; green "upper→log"

  run "upper→rot→log" $'modular pipeline\n<END>\n' "$A 12 uppercaser rotator logger"
  rc 0; haso "[logger] EMODULAR PIPELIN"; last_is "Pipeline shutdown complete"; e_empty; green "upper→rot→log"

  run "flip→upper→log" $'Go fast, not slow\n<END>\n' "$A 12 flipper uppercaser logger"
  rc 0; haso "[logger] WOLS TON ,TSAF OG"; last_is "Pipeline shutdown complete"; e_empty; green "flip→upper→log"

  run "rot→expand→log" $'ABC\n<END>\n' "$A 8 rotator expander logger"
  rc 0; haso "[logger] C A B"; last_is "Pipeline shutdown complete"; e_empty; green "rot→expand→log"
  
  # ---- ordering & CRLF ----
  run "order" $'first\nsecond\nthird\n<END>\n' "$A 10 uppercaser logger"
  rc 0
  grep -Fq $'[logger] FIRST\n[logger] SECOND\n[logger] THIRD' <<<"$(printf "%s" "$OUT" | sed '$d')" \
    || red "order mismatch"
  last_is "Pipeline shutdown complete"; e_empty; green "order"

  run "crlf" $'a\r\n<END>\r\n' "$A 8 uppercaser logger"
  rc 0; haso "[logger] A"; last_is "Pipeline shutdown complete"; e_empty; green "crlf"

  # ---- long line (1024) ----
  long_in="$(head -c 1024 </dev/zero | tr '\0' 'x')"
  run "long 1024" "$(printf "%s\n<END>\n" "$long_in")" "$A 16 uppercaser logger"
  rc 0
  payload="$(grep -o '^\[logger\] .*' <<<"$OUT" | head -n1 | sed 's/^\[logger\] //')"
  [[ ${#payload} -eq 1024 ]] || red "payload len ${#payload} (want 1024)"
  [[ "$payload" == "$(head -c 1024 </dev/zero | tr '\0' 'X')" ]] || red "payload content mismatch"
  last_is "Pipeline shutdown complete"; e_empty; green "long 1024"

  # ---- no sink ----
  run "no sink" $'hello world\n<END>\n' "$A 8 uppercaser flipper"
  rc 0
  ! grep -Eq '^\[(logger|typewriter)\]' <<<"$OUT" || red "sink output found"
  last_is "Pipeline shutdown complete"; e_empty; green "no sink"

  # ---- stress / backpressure ----
  many="$( (seq 1 30 | sed 's/^/m/'); echo '<END>' )"
  run "stress q=2" "$many" "$A 2 flipper rotator logger"
  rc 0
  [[ "$(grep -c '^\[logger\]' <<<"$OUT" || true)" -eq 30 ]] || red "sink count != 30"
  last_is "Pipeline shutdown complete"; e_empty; green "stress q=2"

  # ---- no <END>: should wait (use timeout if present) ----
  if command -v timeout >/dev/null 2>&1; then
    run "waits w/o END" "" "timeout 1s $A 8 uppercaser logger < /dev/null"
    [[ $RC -eq 124 ]] || red "expected timeout rc=124"
    o_empty; e_empty; green "waits w/o END"
  fi

  green "all good"
}

main "$@"
