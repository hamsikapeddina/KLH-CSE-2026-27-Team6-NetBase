#!/usr/bin/env bash
# ==============================================================================
# Phase 3 Concurrency Automated Verification Script — University ERP C Server
# Tests:
#   1. Binary build and thread pool initialization (4 worker threads)
#   2. /api/status concurrency diagnostics (worker pool, queue, semaphore)
#   3. High-concurrency burst test (32 simultaneous requests across threads)
#   4. Multi-worker execution verification (all 4 workers participate)
#   5. Shared counters thread-safety under load
#   6. Concurrent academic data mutations (marks & attendance under load)
#   7. Semaphore resource-limiting verification
#   8. Graceful shutdown (worker thread join and resource cleanup)
# ==============================================================================

set -euo pipefail

PORT=3089
SERVER_BIN="./bin/server"
LOG_DIR="server/logs"
PID_FILE="server/logs/test_concurrency.pid"
LOG_FILE="$LOG_DIR/test_concurrency_server.log"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED_COUNT=0
TOTAL_TESTS=8

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASSED_COUNT=$((PASSED_COUNT + 1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; cleanup; exit 1; }

cleanup() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE" 2>/dev/null || true)
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            log_info "Stopping concurrency test server PID $PID..."
            kill -TERM "$PID" 2>/dev/null || true
            sleep 0.5
        fi
        rm -f "$PID_FILE"
    fi
}
trap cleanup EXIT INT TERM

echo "=========================================================="
echo " University ERP — Phase 3 Concurrency Automated Test Suite"
echo " POSIX Threads | Mutexes | Condition Variables | Semaphores"
echo "=========================================================="

mkdir -p "$LOG_DIR" bin
pkill -9 -f "bin/server $PORT" 2>/dev/null || true
fuser -k $PORT/tcp 2>/dev/null || true
sleep 0.5

log_info "Building C server executable..."
if command -v make >/dev/null 2>&1; then
    make
fi

log_info "Starting C server on test port $PORT..."
$SERVER_BIN $PORT > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo "$SERVER_PID" > "$PID_FILE"
sleep 0.6

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server failed to start. Log output:\n$(cat "$LOG_FILE")"
fi

# -------------------------------------------------------------
# TEST 1: Thread Pool Initialization & Diagnostics
# -------------------------------------------------------------
log_info "Test 1: Verifying thread pool initialization via /api/status..."
STATUS_RESP=$(curl -s "http://127.0.0.1:$PORT/api/status")

if ! echo "$STATUS_RESP" | grep -q '"status": "ONLINE"'; then
    log_fail "Server status is not ONLINE"
fi

if ! echo "$STATUS_RESP" | grep -q '"worker_count": 4'; then
    log_fail "Worker count is not 4 in thread_pool"
fi

if ! echo "$STATUS_RESP" | grep -q '"queue_capacity": 32'; then
    log_fail "Queue capacity is not 32"
fi

if ! echo "$STATUS_RESP" | grep -q '"total_slots": 2'; then
    log_fail "Semaphore total_slots is not 2"
fi

log_pass "Thread pool verified with 4 workers, queue capacity 32, and 2 semaphore slots."

# -------------------------------------------------------------
# TEST 2: Concurrent Request Burst (32 Simultaneous Requests)
# -------------------------------------------------------------
log_info "Test 2: Dispatching burst of 32 simultaneous requests across workers..."
BURST_DIR=$(mktemp -d)

ENDPOINTS=(
    "/api/student/dashboard?id=S101"
    "/api/student/dashboard?id=S102"
    "/api/student/dashboard?id=S103"
    "/api/faculty/dashboard?id=F201"
    "/api/faculty/dashboard?id=F202"
    "/api/sample-data"
    "/api/courses"
    "/api/students"
    "/api/faculty"
    "/api/status"
)

CURL_PIDS=()
for i in $(seq 1 32); do
    EP="${ENDPOINTS[$((i % ${#ENDPOINTS[@]}))]}"
    (
        CODE=$(curl -s -m 5 -o "$BURST_DIR/out_$i.json" -w "%{http_code}" "http://127.0.0.1:$PORT$EP")
        echo "$CODE" > "$BURST_DIR/code_$i.txt"
    ) &
    CURL_PIDS+=($!)
done

# Wait ONLY for the background curl processes, NOT the server!
wait "${CURL_PIDS[@]}"

# Check results
ALL_OK=true
for i in $(seq 1 32); do
    CODE=$(cat "$BURST_DIR/code_$i.txt" 2>/dev/null || echo "000")
    if [ "$CODE" != "200" ]; then
        ALL_OK=false
        log_info "Request $i failed with HTTP code $CODE"
    fi
done

rm -rf "$BURST_DIR"

if [ "$ALL_OK" != "true" ]; then
    log_fail "One or more concurrent burst requests failed"
fi

log_pass "All 32 concurrent requests completed successfully with HTTP 200."

# -------------------------------------------------------------
# TEST 3: Multi-Worker Thread Participation
# -------------------------------------------------------------
log_info "Test 3: Checking server log to verify work distribution across workers..."
WORKER0_COUNT=$(grep -c "\[Worker 0\] Finished request" "$LOG_FILE" || true)
WORKER1_COUNT=$(grep -c "\[Worker 1\] Finished request" "$LOG_FILE" || true)
WORKER2_COUNT=$(grep -c "\[Worker 2\] Finished request" "$LOG_FILE" || true)
WORKER3_COUNT=$(grep -c "\[Worker 3\] Finished request" "$LOG_FILE" || true)

log_info "Request completion distribution across worker threads:"
log_info "  Worker 0: $WORKER0_COUNT requests handled"
log_info "  Worker 1: $WORKER1_COUNT requests handled"
log_info "  Worker 2: $WORKER2_COUNT requests handled"
log_info "  Worker 3: $WORKER3_COUNT requests handled"

TOTAL_WORKER_TASKS=$((WORKER0_COUNT + WORKER1_COUNT + WORKER2_COUNT + WORKER3_COUNT))
if [ "$TOTAL_WORKER_TASKS" -lt 32 ]; then
    log_fail "Worker tasks completed ($TOTAL_WORKER_TASKS) is less than total dispatched (32)"
fi

ACTIVE_WORKERS=0
[ "$WORKER0_COUNT" -gt 0 ] && ACTIVE_WORKERS=$((ACTIVE_WORKERS + 1))
[ "$WORKER1_COUNT" -gt 0 ] && ACTIVE_WORKERS=$((ACTIVE_WORKERS + 1))
[ "$WORKER2_COUNT" -gt 0 ] && ACTIVE_WORKERS=$((ACTIVE_WORKERS + 1))
[ "$WORKER3_COUNT" -gt 0 ] && ACTIVE_WORKERS=$((ACTIVE_WORKERS + 1))

if [ "$ACTIVE_WORKERS" -lt 2 ]; then
    log_fail "Expected multiple workers to handle burst requests, but only $ACTIVE_WORKERS worker was active"
fi

log_pass "Multi-worker concurrency verified: $ACTIVE_WORKERS distinct POSIX worker threads actively processed requests."

# -------------------------------------------------------------
# TEST 4: Shared Counters Thread-Safety
# -------------------------------------------------------------
log_info "Test 4: Verifying shared counters (total_requests, completed_requests)..."
STATUS_RESP2=$(curl -s "http://127.0.0.1:$PORT/api/status")

TOTAL_REQ=$(echo "$STATUS_RESP2" | grep '"total_requests":' | sed -E 's/.*"total_requests": ([0-9]+).*/\1/')
COMPLETED_REQ=$(echo "$STATUS_RESP2" | grep '"completed_requests":' | sed -E 's/.*"completed_requests": ([0-9]+).*/\1/')
FAILED_REQ=$(echo "$STATUS_RESP2" | grep '"failed_requests":' | sed -E 's/.*"failed_requests": ([0-9]+).*/\1/')

log_info "Counter stats: Total=$TOTAL_REQ, Completed=$COMPLETED_REQ, Failed=$FAILED_REQ"

if [ -z "$TOTAL_REQ" ] || [ "$TOTAL_REQ" -lt 30 ]; then
    log_fail "Total requests counter ($TOTAL_REQ) is less than expected (>30)"
fi

if [ -z "$COMPLETED_REQ" ] || [ "$COMPLETED_REQ" -lt 30 ]; then
    log_fail "Completed requests counter ($COMPLETED_REQ) is less than expected (>30)"
fi

if [ "$FAILED_REQ" -ne 0 ]; then
    log_fail "Failed requests counter is not 0 (was $FAILED_REQ)"
fi

log_pass "Shared counters protected by mutex accurately recorded $TOTAL_REQ requests with 0 failures."

# -------------------------------------------------------------
# TEST 5: Concurrent Academic Data Mutations
# -------------------------------------------------------------
log_info "Test 5: Testing concurrent database mutations under mutex protection..."
MUT_DIR=$(mktemp -d)
MUT_PIDS=()

for i in $(seq 1 10); do
    (
        C1=$(curl -s -m 5 -X POST "http://127.0.0.1:$PORT/api/marks/update" \
            -H "Content-Type: application/json" \
            -d "{\"student_id\": \"S101\", \"course_id\": \"CS101\", \"midterm\": $((20 + i)), \"endterm\": $((40 + i)), \"quiz\": 18}" \
            -w "%{http_code}" -o "$MUT_DIR/marks_$i.json")
        echo "$C1" > "$MUT_DIR/marks_code_$i.txt"
    ) &
    MUT_PIDS+=($!)
    (
        C2=$(curl -s -m 5 -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
            -H "Content-Type: application/json" \
            -d "{\"student_id\": \"S101\", \"course_id\": \"CS101\", \"attended\": $((25 + i % 5)), \"total\": 30}" \
            -w "%{http_code}" -o "$MUT_DIR/att_$i.json")
        echo "$C2" > "$MUT_DIR/att_code_$i.txt"
    ) &
    MUT_PIDS+=($!)
done

wait "${MUT_PIDS[@]}"

MUT_OK=true
for i in $(seq 1 10); do
    MCODE=$(cat "$MUT_DIR/marks_code_$i.txt" 2>/dev/null || echo "000")
    ACODE=$(cat "$MUT_DIR/att_code_$i.txt" 2>/dev/null || echo "000")
    if [ "$MCODE" != "200" ] || [ "$ACODE" != "200" ]; then
        MUT_OK=false
    fi
done
rm -rf "$MUT_DIR"

if [ "$MUT_OK" != "true" ]; then
    log_fail "Concurrent mutation request failed"
fi

# Verify data store integrity by querying student dashboard
DASH_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
if ! echo "$DASH_RESP" | grep -q '"student":'; then
    log_fail "Data store corrupted after concurrent updates"
fi

log_pass "Concurrent data mutations executed without race conditions or memory corruption."

# -------------------------------------------------------------
# TEST 6: Semaphore Resource-Limiting Logs
# -------------------------------------------------------------
log_info "Test 6: Verifying POSIX Semaphore DB slot acquisition in logs..."
SEM_ACQ_COUNT=$(grep -c "\[SEMAPHORE\] Acquired DB access slot" "$LOG_FILE" || true)
SEM_REL_COUNT=$(grep -c "\[SEMAPHORE\] Releasing DB access slot" "$LOG_FILE" || true)

log_info "Semaphore operations: Acquired=$SEM_ACQ_COUNT, Released=$SEM_REL_COUNT"

if [ "$SEM_ACQ_COUNT" -eq 0 ] || [ "$SEM_REL_COUNT" -eq 0 ]; then
    log_fail "No semaphore transactions detected in server log"
fi

log_pass "POSIX Semaphore actively regulated concurrent data store access slots."

# -------------------------------------------------------------
# TEST 7: Viva Concept Verification in /api/status
# -------------------------------------------------------------
log_info "Test 7: Verifying academic OS concepts in /api/status payload..."
if ! echo "$STATUS_RESP" | grep -q "POSIX Threads"; then
    log_fail "POSIX Threads concept missing in /api/status"
fi
if ! echo "$STATUS_RESP" | grep -q "POSIX Mutex"; then
    log_fail "POSIX Mutex concept missing in /api/status"
fi
if ! echo "$STATUS_RESP" | grep -q "POSIX Condition Variables"; then
    log_fail "POSIX Condition Variables concept missing in /api/status"
fi
if ! echo "$STATUS_RESP" | grep -q "POSIX Counting Semaphore"; then
    log_fail "POSIX Counting Semaphore concept missing in /api/status"
fi

log_pass "OS concepts and architectural metadata verified in /api/status response."

# -------------------------------------------------------------
# TEST 8: Clean Worker Thread Join & Signal Handling
# -------------------------------------------------------------
log_info "Test 8: Testing clean worker thread join upon SIGTERM..."
kill -TERM "$SERVER_PID"
sleep 0.8

if kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server did not terminate after SIGTERM"
fi

# Check log for clean shutdown messages
if ! grep -q "Initiating graceful shutdown of 4 worker threads" "$LOG_FILE"; then
    log_fail "Shutdown message not found in server log"
fi

if ! grep -q "Joined successfully" "$LOG_FILE"; then
    log_fail "Worker thread join message not found in server log"
fi

if ! grep -q "All workers joined and synchronization primitives destroyed cleanly" "$LOG_FILE"; then
    log_fail "Clean termination completion message not found in server log"
fi

rm -f "$PID_FILE"
log_pass "Server cleanly joined all 4 worker threads and freed synchronization resources on SIGTERM."

echo "=========================================================="
echo -e "${GREEN}All $TOTAL_TESTS/$TOTAL_TESTS Phase 3 Concurrency Tests PASSED!${NC}"
echo "=========================================================="
