#!/usr/bin/env bash
# File-backed check that LOG_FATAL / LOG_ASSERT records reach the log file before abort/exit.
# The gtest death tests run with the console sink, so they cannot observe this.
# Usage: tests/fatal_flush_check.sh [compiler]   (run from the repo root)
set -u
CXX=${1:-clang++}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
EXTRA=()
if [ "$(uname -s)" = Darwin ] && command -v brew >/dev/null; then EXTRA=(-I"$(brew --prefix)/include"); fi
fail=0
for kind in FATAL ASSERT ASSERT_MSG; do
  case $kind in
    FATAL)  body='LOG_FATAL("fatal-record-{}", 1);' ;;
    ASSERT)     body='LOG_ASSERT(1 == 2);' ;;   # logs the condition text, not a custom message
    ASSERT_MSG) body='LOG_ASSERT_MSG(1 == 2, "assert_msg-record-{}", 2);' ;;
  esac
  printf '#include "LOGGER_MACROS.hpp"\nusing namespace logger;\nint main() { %s return 0; }\n' "$body" > "$TMP/$kind.cpp"
  $CXX -std=c++20 -Iinclude "${EXTRA[@]}" -DFILE_LOGGING "-DLOG_FILE_NAME=\"$TMP/$kind.log\"" src/*.cpp "$TMP/$kind.cpp" -o "$TMP/$kind" || { echo "$kind: build failed"; fail=1; continue; }
  (cd "$TMP" && "./$kind" >/dev/null 2>&1); rc=$?
  want=$(echo "$kind" | tr 'A-Z' 'a-z')-record
  [ "$kind" = ASSERT ] && want='CONDITION: 1 == 2'
  if [ "$rc" -ne 0 ] && grep -q "$want" "$TMP/$kind.log" 2>/dev/null; then
    echo "$kind: ok (exit=$rc, record on disk)"
  else
    echo "$kind: FAIL (exit=$rc, record missing from $kind.log)"; fail=1
  fi
done
exit $fail
