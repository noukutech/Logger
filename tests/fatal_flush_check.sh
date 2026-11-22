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
for kind in FATAL ASSERT ASSERT_MSG ASSERT_ABORT ATEXIT_LOG; do
  case $kind in
    FATAL)  body='LOG_FATAL("fatal-record-{}", 1);' ;;
    ASSERT)     body='LOG_ASSERT(1 == 2);' ;;   # logs the condition text, not a custom message
    ASSERT_MSG) body='LOG_ASSERT_MSG(1 == 2, "assert_msg-record-{}", 2);' ;;
    # abort() path: skips static destructors, so only the explicit flush can save the record
    ASSERT_ABORT) body='log_assert(__FILE__, __PRETTY_FUNCTION__, __LINE__, "1 == 2", false, "assert_abort-record-{}", 3);' ;;
    # exit() path with an atexit handler that logs: must not deadlock on the logging mutex
    ATEXIT_LOG) body='std::atexit([]() { LOG_INFO("atexit_log-record-{}", 4); }); LOG_ASSERT(1 == 2);' ;;
  esac
  printf '#include "LOGGER_MACROS.hpp"\n#include <cstdlib>\nusing namespace logger;\nint main() { %s return 0; }\n' "$body" > "$TMP/$kind.cpp"
  $CXX -std=c++20 -Iinclude "${EXTRA[@]}" -DFILE_LOGGING "-DLOG_FILE_NAME=\"$TMP/$kind.log\"" src/*.cpp "$TMP/$kind.cpp" -o "$TMP/$kind" || { echo "$kind: build failed"; fail=1; continue; }
  # Bounded: a deadlock in the exit path must fail the check, not hang it
  (cd "$TMP" && perl -e 'alarm 30; exec @ARGV' "./$kind" >/dev/null 2>&1); rc=$?
  if [ "$rc" -eq 142 ]; then echo "$kind: FAIL (hung, killed after 30s)"; fail=1; continue; fi
  want=$(echo "$kind" | tr 'A-Z' 'a-z')-record
  [ "$kind" = ASSERT ] && want='CONDITION: 1 == 2'
  # The exit-path handler runs after the assertion; reaching it proves no deadlock
  [ "$kind" = ATEXIT_LOG ] && want='atexit_log-record'
  if [ "$rc" -ne 0 ] && grep -q "$want" "$TMP/$kind.log" 2>/dev/null; then
    echo "$kind: ok (exit=$rc, record on disk)"
  else
    echo "$kind: FAIL (exit=$rc, record missing from $kind.log)"; fail=1
  fi
done
exit $fail
