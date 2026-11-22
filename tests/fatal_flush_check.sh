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
for kind in FATAL FATAL_BADFMT FATAL_JSONFMT ASSERT ASSERT_MSG ASSERT_ABORT ATEXIT_LOG; do
  case $kind in
    FATAL)  body='LOG_FATAL("fatal-record-{}", 1);' ;;
    # malformed runtime format: logging must not throw past the abort
    FATAL_BADFMT) body='LOG_FATAL("fatal_badfmt-record {");' ;;
    # quoted malformed format: raw text and diagnostic must both reach the file
    FATAL_JSONFMT) body='LOG_FATAL(R"({"fatal_jsonfmt-record": {})");' ;;
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
  [ "$kind" = FATAL_JSONFMT ] && want='"fatal_jsonfmt-record": {.*format error'
  # Exact status: abort() paths must abort (134 = SIGABRT), graceful paths must exit(1)
  case $kind in FATAL|FATAL_BADFMT|FATAL_JSONFMT|ASSERT_ABORT) expect=134 ;; *) expect=1 ;; esac
  if [ "$rc" -eq "$expect" ] && grep -q "$want" "$TMP/$kind.log" 2>/dev/null; then
    echo "$kind: ok (exit=$rc, record on disk)"
  else
    echo "$kind: FAIL (exit=$rc, expected $expect; or record missing from $kind.log)"; fail=1
  fi
done
# Sink failure: the file opens but every write fails (file-size limit 0, SIGXFSZ ignored).
# The fatal record cannot reach disk, so it must be reported on stderr before abort().
# The record carries an ESC byte: it must reach stderr escaped, never as a raw terminal escape
# The host makes stderr fully buffered: the fallback must flush it, or abort() discards it.
printf '#include "LOGGER_MACROS.hpp"\n#include <cstdio>\nusing namespace logger;\nstatic char buf[1 << 16];\nint main() { std::setvbuf(stderr, buf, _IOFBF, sizeof(buf)); LOG_FATAL("fatal_sinkfail-record \\x1b[31mred"); return 0; }\n' > "$TMP/SINKFAIL.cpp"
if $CXX -std=c++20 -Iinclude "${EXTRA[@]}" -DFILE_LOGGING "-DLOG_FILE_NAME=\"$TMP/SINKFAIL.log\"" src/*.cpp "$TMP/SINKFAIL.cpp" -o "$TMP/SINKFAIL"; then
  # stderr goes through a pipe to a reader outside the limit (a capture file would hit the limit too)
  (cd "$TMP" && trap '' XFSZ && ulimit -f 0 && perl -e 'alarm 30; exec @ARGV' ./SINKFAIL 2>&1 >/dev/null) | cat > "$TMP/SINKFAIL.err"; rc=${PIPESTATUS[0]}
  if [ "$rc" -eq 134 ] && grep -q 'fatal_sinkfail-record.*sink failed to write this record' "$TMP/SINKFAIL.err" \
     && grep -qF '\x1b[31m' "$TMP/SINKFAIL.err" && ! LC_ALL=C grep -q "$(printf '\033')" "$TMP/SINKFAIL.err"; then
    echo "FATAL_SINKFAIL: ok (exit=$rc, record reported on stderr, control chars escaped)"
  else
    echo "FATAL_SINKFAIL: FAIL (exit=$rc, expected 134 and the escaped record on stderr)"; fail=1
  fi
else
  echo "FATAL_SINKFAIL: build failed"; fail=1
fi
# An unrelated failure recorded *before* the fatal record must not echo the record to stderr:
# the fatal record itself is written fine, so it belongs only in the log file.
printf '#include "LOGGER_MACROS.hpp"\n#include <stdexcept>\nusing namespace logger;\nint main() { loggingOps.addRaisedException(std::make_exception_ptr(std::runtime_error("earlier"))); LOG_FATAL("fatal_priorfail-record"); return 0; }\n' > "$TMP/PRIORFAIL.cpp"
if $CXX -std=c++20 -Iinclude "${EXTRA[@]}" -DFILE_LOGGING "-DLOG_FILE_NAME=\"$TMP/PRIORFAIL.log\"" src/*.cpp "$TMP/PRIORFAIL.cpp" -o "$TMP/PRIORFAIL"; then
  (cd "$TMP" && perl -e 'alarm 30; exec @ARGV' ./PRIORFAIL >/dev/null 2>"$TMP/PRIORFAIL.err"); rc=$?
  if [ "$rc" -eq 134 ] && grep -q 'fatal_priorfail-record' "$TMP/PRIORFAIL.log" && ! grep -q 'fatal_priorfail-record' "$TMP/PRIORFAIL.err"; then
    echo "FATAL_PRIORFAIL: ok (exit=$rc, record in file only, not echoed to stderr)"
  else
    echo "FATAL_PRIORFAIL: FAIL (exit=$rc, expected 134, record in file, nothing on stderr)"; fail=1
  fi
else
  echo "FATAL_PRIORFAIL: build failed"; fail=1
fi
exit $fail
