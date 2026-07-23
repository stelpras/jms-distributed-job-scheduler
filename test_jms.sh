#!/bin/bash
#
# test_jms.sh — smoke test for the job management system (jms_coord + jms_console)
#
# Starts the coordinator in background, connects a console, submits some jobs
# (mix of quick commands and longer sleeps to test queueing), checks that
# workers handle them, verifies output files are created, then shuts down
# and cleans up. The test runs under time bounds so it doesn't hang forever.
#
# Usage: ./test_jms.sh

set -uo pipefail

PORT=9000
COORD_TIMEOUT=30   # seconds to let coordinator run
LOG_DIR="./test_jms_out"

PASS=0
FAIL=0

check() {
    local desc="$1"
    local condition="$2"   # 0 = pass, anything else = fail
    if [ "$condition" -eq 0 ]; then
        echo "  [PASS] $desc"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $desc"
        FAIL=$((FAIL + 1))
    fi
}

cleanup() {
    # Kill any lingering coordinator or console
    pkill -9 -f "jms_coord.*-p $PORT" > /dev/null 2>&1 || true
    pkill -9 -f "jms_console" > /dev/null 2>&1 || true
    sleep 1
}

# --- setup -------------------------------------------------------------

echo "== Building jms_coord and jms_console =="
cleanup
if ! make > build.log 2>&1; then
    echo "Build failed — see build.log"
    exit 1
fi
echo "Build OK"
echo

rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"

# --- start coordinator in background ------------------------------------

echo "== Starting coordinator =="
# Run coordinator in background, redirecting output to a log
timeout "$COORD_TIMEOUT" ./jms_coord -p $PORT -l "$LOG_DIR" -n 2 > coord.log 2>&1 &
COORD_PID=$!
sleep 2   # give it time to bind and listen

check "coordinator is running"                     $(kill -0 $COORD_PID 2>/dev/null && echo 0 || echo 1)
check "coordinator is listening on port $PORT"     $(ss -tlnp 2>/dev/null | grep -q ":$PORT " && echo 0 || echo 1)
echo

# --- submit jobs via console --------------------------------------------

echo "== Submitting jobs via console =="
CONSOLE_INPUT="
submit echo hello
submit sleep 2
submit echo world
status 1
show-active
show-workers
"

# Send commands to the console (which connects to the coordinator and processes them)
CONSOLE_OUTPUT=$(echo "$CONSOLE_INPUT" | timeout 15 ./jms_console -h localhost -p $PORT 2>&1)

check "console got responses from coordinator"     $([ -n "$CONSOLE_OUTPUT" ] && echo 0 || echo 1)
check "first job was submitted (got a JobID)"      $(echo "$CONSOLE_OUTPUT" | grep -q "JobID:" && echo 0 || echo 1)
check "show-active returned results"               $(echo "$CONSOLE_OUTPUT" | grep -q "Active" && echo 0 || echo 1)
check "show-workers returned results"              $(echo "$CONSOLE_OUTPUT" | grep -qE "idle|running|served" && echo 0 || echo 1)
echo

# --- check output files were created ------------------------------------

echo "== Checking job output files =="
sleep 3   # let jobs finish

OUTPUT_DIRS=$(find "$LOG_DIR" -mindepth 1 -maxdepth 1 -type d -name "outputs_*" | wc -l)
check "at least one output directory was created"  $([ "$OUTPUT_DIRS" -ge 1 ] && echo 0 || echo 1)

if [ "$OUTPUT_DIRS" -ge 1 ]; then
    FIRST_OUTPUT=$(find "$LOG_DIR" -mindepth 1 -maxdepth 1 -type d -name "outputs_*" | head -n1)
    check "output directory contains stdout file"  $([ -f "$FIRST_OUTPUT"/stdout_* ] && echo 0 || echo 1)
    check "output directory contains stderr file"  $([ -f "$FIRST_OUTPUT"/stderr_* ] && echo 0 || echo 1)
fi
echo

# --- shutdown coordinator -----------------------------------------------

echo "== Shutting down =="
# Send shutdown command through console
echo "shutdown" | timeout 10 ./jms_console -h localhost -p $PORT > /dev/null 2>&1

sleep 2
check "coordinator exited after shutdown"          $(kill -0 $COORD_PID 2>/dev/null && echo 1 || echo 0)
check "coordinator printed served-jobs summary"    $(grep -q "Served" coord.log && echo 0 || echo 1)
echo

# --- valgrind checks (if available) -----------------------------------------

echo "== Memory checks with valgrind =="
if command -v valgrind > /dev/null 2>&1; then
    cleanup
    rm -rf "$LOG_DIR" coord.log
    
    # Run coordinator under valgrind for a short time
    VALGRIND_LOG="valgrind-jms_coord.log"
    timeout 5 valgrind --leak-check=full --show-leak-kinds=all --log-file="$VALGRIND_LOG" \
        ./jms_coord -p $PORT -l "$LOG_DIR" -n 2 > /dev/null 2>&1 &
    VGPID=$!
    sleep 2
    
    # Send a couple of jobs then shutdown
    (echo "submit echo test"; sleep 1; echo "shutdown") | timeout 5 ./jms_console -h localhost -p $PORT > /dev/null 2>&1
    
    sleep 3
    kill -9 $VGPID 2>/dev/null || true
    
    if [ -f "$VALGRIND_LOG" ]; then
        check "valgrind: no memory errors in coordinator"   $(grep -q "ERROR SUMMARY: 0 errors" "$VALGRIND_LOG" && echo 0 || echo 1)
    else
        echo "  [SKIP] valgrind log not created"
    fi
else
    echo "  [SKIP] valgrind not installed"
fi
echo

# --- cleanup ---------------------------------------------------------------

cleanup
rm -rf "$LOG_DIR" coord.log build.log valgrind-*.log

# --- summary ---------------------------------------------------------------

echo "===================================="
echo "Passed: $PASS   Failed: $FAIL"
echo "===================================="
echo "Note: this is a short smoke test, not a comprehensive load test."
echo "For a full test, run the coordinator manually and submit many jobs."

exit $FAIL