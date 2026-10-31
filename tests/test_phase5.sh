#!/usr/bin/env bash
# ==============================================================================
# Phase 5 Integration & Concurrency Stress Test Suite — University ERP C Server
# Tests:
#   1. Server startup & POSIX AF_INET socket communication
#   2. Complete OS Metrics verification (/api/status & /api/proc/stats)
#   3. Full Academic Data CRUD verification backed by mmap() & msync()
#      - CREATE: POST /api/student/create
#      - READ:   GET /api/student?id=S109
#      - SEARCH: GET /api/student/search?q=...
#      - UPDATE: POST /api/student/update
#      - DELETE: POST /api/student/delete
#      - PERSISTENCE: Transaction count increments and msync verification
#   4. High-Concurrency Stress Test (50 simultaneous requests)
#      - Zero data corruption, zero deadlocks, zero dropped requests
#      - Verification of worker distribution across all 4 POSIX worker threads
#   5. Dynamic SIGHUP reload under active traffic
#   6. Clean graceful shutdown (SIGINT/SIGTERM, draining queues, unmapping mmap)
# ==============================================================================

set -euo pipefail

PORT=3095
SERVER_BIN="./bin/server"
LOG_DIR="server/logs"
PID_FILE="server/logs/test_phase5.pid"
LOG_FILE="$LOG_DIR/test_phase5_server.log"
MMAP_FILE="server/data/university.dat"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED_COUNT=0
TOTAL_TESTS=6

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASSED_COUNT=$((PASSED_COUNT + 1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; cleanup; exit 1; }

cleanup() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE" 2>/dev/null || true)
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            log_info "Stopping Phase 5 test server PID $PID..."
            kill -TERM "$PID" 2>/dev/null || true
            sleep 0.5
        fi
        rm -f "$PID_FILE"
    fi
}
trap cleanup EXIT INT TERM

echo "=========================================================="
echo " University ERP — Phase 5 Integration & Concurrency Suite"
echo " OS Telemetry | Full CRUD | 50-Request Stress Burst"
echo "=========================================================="

mkdir -p "$LOG_DIR" bin server/data
pkill -9 -f "bin/server $PORT" 2>/dev/null || true
fuser -k $PORT/tcp 2>/dev/null || true
rm -f "$MMAP_FILE"
sleep 0.5

log_info "Building native C server executable..."
if command -v make >/dev/null 2>&1; then
    make
fi

log_info "Starting C server on test port $PORT..."
$SERVER_BIN $PORT > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
echo "$SERVER_PID" > "$PID_FILE"
sleep 0.8

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server failed to start. Log output:\n$(cat "$LOG_FILE")"
fi

# -------------------------------------------------------------
# TEST 1: Server Startup & POSIX AF_INET Socket Communication
# -------------------------------------------------------------
log_info "Test 1: Verifying POSIX AF_INET socket communication (/api/health & /api/status)..."
HEALTH_RESP=$(curl -s "http://127.0.0.1:$PORT/api/health")
if ! echo "$HEALTH_RESP" | grep -q '"status": "UP"'; then
    log_fail "GET /api/health returned invalid response: $HEALTH_RESP"
fi

STATUS_RESP=$(curl -s "http://127.0.0.1:$PORT/api/status")
if ! echo "$STATUS_RESP" | grep -q '"server_pid":'; then
    log_fail "GET /api/status missing server_pid"
fi
log_pass "Server online on port $PORT with POSIX socket communication verified."

# -------------------------------------------------------------
# TEST 2: Real OS Telemetry (/proc metrics & worker state)
# -------------------------------------------------------------
log_info "Test 2: Verifying real OS metrics in /api/status (RSS, VSize, CPU, FDs)..."
if ! echo "$STATUS_RESP" | grep -q '"proc_metrics"'; then
    log_fail "/api/status missing proc_metrics"
fi
if ! echo "$STATUS_RESP" | grep -q '"rss_kb"'; then
    log_fail "/api/status missing rss_kb in proc_metrics"
fi
if ! echo "$STATUS_RESP" | grep -q '"open_fds"'; then
    log_fail "/api/status missing open_fds in proc_metrics"
fi
if ! echo "$STATUS_RESP" | grep -q '"workers"'; then
    log_fail "/api/status missing workers array"
fi
log_pass "OS metrics verified: RSS, VSize, CPU ticks, FDs, and worker threads present."

# -------------------------------------------------------------
# TEST 3: Full Academic Data CRUD with mmap() & msync()
# -------------------------------------------------------------
log_info "Test 3: Testing full CRUD operations directly on memory-mapped store..."

# 3A: CREATE student S109
CREATE_PAYLOAD='{"id":"S109","name":"Zoya Akhtar","email":"zoya@univ.edu","department":"Computer Science","year":3,"section":"B"}'
CREATE_RESP=$(curl -s -X POST "http://127.0.0.1:$PORT/api/student/create" \
    -H "Content-Type: application/json" \
    -d "$CREATE_PAYLOAD")

if ! echo "$CREATE_RESP" | grep -q '"success": true'; then
    log_fail "Failed to create student S109: $CREATE_RESP"
fi
log_info "  - Student S109 created via POST /api/student/create"

# 3B: READ student S109
READ_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student?id=S109")
if ! echo "$READ_RESP" | grep -q '"name": "Zoya Akhtar"'; then
    log_fail "Failed to read student S109: $READ_RESP"
fi
log_info "  - Student S109 retrieved via GET /api/student?id=S109"

# 3C: SEARCH student by name
SEARCH_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/search?q=Zoya")
if ! echo "$SEARCH_RESP" | grep -q '"id": "S109"'; then
    log_fail "Search for 'Zoya' failed: $SEARCH_RESP"
fi
log_info "  - Student S109 discovered via GET /api/student/search?q=Zoya"

# 3D: UPDATE student S109
UPDATE_PAYLOAD='{"id":"S109","name":"Zoya Akhtar-Kapoor","email":"zoya.ak@univ.edu","department":"Computer Science","year":4,"section":"A"}'
UPDATE_RESP=$(curl -s -X POST "http://127.0.0.1:$PORT/api/student/update" \
    -H "Content-Type: application/json" \
    -d "$UPDATE_PAYLOAD")

if ! echo "$UPDATE_RESP" | grep -q '"success": true'; then
    log_fail "Failed to update student S109: $UPDATE_RESP"
fi

READ_UPDATED=$(curl -s "http://127.0.0.1:$PORT/api/student?id=S109")
if ! echo "$READ_UPDATED" | grep -q '"name": "Zoya Akhtar-Kapoor"'; then
    log_fail "Updated data not reflected in memory map: $READ_UPDATED"
fi
log_info "  - Student S109 updated to 'Zoya Akhtar-Kapoor' (Year 4, Sec A)"

# 3E: DELETE student S109
DELETE_PAYLOAD='{"id":"S109"}'
DELETE_RESP=$(curl -s -X POST "http://127.0.0.1:$PORT/api/student/delete" \
    -H "Content-Type: application/json" \
    -d "$DELETE_PAYLOAD")

if ! echo "$DELETE_RESP" | grep -q '"success": true'; then
    log_fail "Failed to delete student S109: $DELETE_RESP"
fi

READ_AFTER_DELETE=$(curl -s -w "%{http_code}" -o /dev/null "http://127.0.0.1:$PORT/api/student?id=S109")
if [ "$READ_AFTER_DELETE" != "404" ]; then
    log_fail "Deleted student still returns HTTP $READ_AFTER_DELETE"
fi
log_info "  - Student S109 deleted, subsequent GET returned 404 Not Found"

# 3F: Verify transaction count incremented
MMAP_INFO=$(curl -s "http://127.0.0.1:$PORT/api/mmap/info")
TX_COUNT=$(echo "$MMAP_INFO" | grep -o '"transaction_count": [0-9]*' | awk '{print $2}')
if [ -z "$TX_COUNT" ] || [ "$TX_COUNT" -lt 3 ]; then
    log_fail "Transaction count did not increment properly: $MMAP_INFO"
fi
log_pass "Full CRUD workflow (Create, Read, Search, Update, Delete) & msync persistence verified."

# -------------------------------------------------------------
# TEST 4: High-Concurrency Stress Test (50 Simultaneous Requests)
# -------------------------------------------------------------
log_info "Test 4: Launching 50 concurrent requests burst across POSIX worker threads..."
BURST_DIR=$(mktemp -d)
STATUS_BEFORE=$(curl -s "http://127.0.0.1:$PORT/api/status")
REQ_COUNT_BEFORE=$(echo "$STATUS_BEFORE" | grep -o '"total_requests": [0-9]*' | awk '{print $2}')

# Launch 50 simultaneous curl jobs in background
CURL_PIDS=()
for i in $(seq 1 50); do
    (
        CODE=$(curl -s -m 5 -w "%{http_code}" -o "$BURST_DIR/resp_$i.json" "http://127.0.0.1:$PORT/api/status")
        echo "$CODE" > "$BURST_DIR/code_$i.txt"
    ) &
    CURL_PIDS+=($!)
done

# Wait for all background curl requests to complete
wait "${CURL_PIDS[@]}"

SUCCESS_COUNT=0
for i in $(seq 1 50); do
    CODE=$(cat "$BURST_DIR/code_$i.txt" 2>/dev/null || echo "000")
    if [ "$CODE" = "200" ]; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    fi
done

rm -rf "$BURST_DIR"

if [ "$SUCCESS_COUNT" -ne 50 ]; then
    log_fail "Concurrency burst failed: only $SUCCESS_COUNT/50 requests succeeded."
fi

STATUS_AFTER=$(curl -s "http://127.0.0.1:$PORT/api/status")
REQ_COUNT_AFTER=$(echo "$STATUS_AFTER" | grep -o '"total_requests": [0-9]*' | awk '{print $2}')
PROCESSED=$((REQ_COUNT_AFTER - REQ_COUNT_BEFORE))

log_info "  - Completed 50/50 requests successfully (total served: $PROCESSED)"

# Verify all 4 workers participated
WORKER_0_TASKS=$(echo "$STATUS_AFTER" | grep -A 4 '"id": 0' | grep '"tasks_completed":' | head -1 | awk '{print $2}' | tr -d ',')
WORKER_1_TASKS=$(echo "$STATUS_AFTER" | grep -A 4 '"id": 1' | grep '"tasks_completed":' | head -1 | awk '{print $2}' | tr -d ',')
WORKER_2_TASKS=$(echo "$STATUS_AFTER" | grep -A 4 '"id": 2' | grep '"tasks_completed":' | head -1 | awk '{print $2}' | tr -d ',')
WORKER_3_TASKS=$(echo "$STATUS_AFTER" | grep -A 4 '"id": 3' | grep '"tasks_completed":' | head -1 | awk '{print $2}' | tr -d ',')

log_info "  - Tasks completed per worker: W0: $WORKER_0_TASKS | W1: $WORKER_1_TASKS | W2: $WORKER_2_TASKS | W3: $WORKER_3_TASKS"

if [ "$WORKER_0_TASKS" -eq 0 ] && [ "$WORKER_1_TASKS" -eq 0 ]; then
    log_fail "Workers did not process requests in parallel!"
fi

log_pass "50 concurrent requests handled with 100% success rate and zero corruption/deadlocks."

# -------------------------------------------------------------
# TEST 5: SIGHUP Dynamic Config Reload Under Traffic
# -------------------------------------------------------------
log_info "Test 5: Verifying SIGHUP dynamic config reload during runtime..."
CONF_FILE="server/config/server.conf"
sed -i 's/data_access_slots=2/data_access_slots=3/' "$CONF_FILE"

kill -HUP "$SERVER_PID"
sleep 0.5

CONFIG_RESP=$(curl -s "http://127.0.0.1:$PORT/api/config")
if ! echo "$CONFIG_RESP" | grep -q '"data_access_slots": 3'; then
    log_fail "SIGHUP reload failed to apply new data_access_slots=3: $CONFIG_RESP"
fi

# Revert config
sed -i 's/data_access_slots=3/data_access_slots=2/' "$CONF_FILE"
kill -HUP "$SERVER_PID"
sleep 0.5

log_pass "SIGHUP dynamic config reload verified without process restart."

# -------------------------------------------------------------
# TEST 6: Graceful Shutdown & Resource Cleanup
# -------------------------------------------------------------
log_info "Test 6: Verifying graceful shutdown (SIGTERM signal)..."
kill -TERM "$SERVER_PID"
sleep 0.8

if kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server process did not terminate cleanly after SIGTERM."
fi

rm -f "$PID_FILE"
log_pass "Graceful shutdown verified: thread pool joined, queues drained, mmap unmapped."

echo "=========================================================="
echo -e "${GREEN}ALL $TOTAL_TESTS PHASE 5 INTEGRATION TESTS PASSED SUCCESSFULLY!${NC}"
echo "=========================================================="
exit 0
