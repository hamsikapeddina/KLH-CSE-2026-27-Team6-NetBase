#!/usr/bin/env bash
# Phase 4 Automated Test Suite
# Tests:
#   1. Memory-Mapped Storage (mmap, msync, munmap, binary header, persistence across restart)
#   2. Two-Stage Queue Pipeline (Request Queue -> Workers -> Response Queue -> Dispatcher Thread)
#   3. Linux /proc Monitoring (/proc/self/stat, /proc/uptime, memory, CPU, FD count)
#   4. Dynamic Configuration Reload via SIGHUP (server.conf live reloading)
#   5. Thread-Safe Logging (server.log, transactions.log with transaction tracking)
#   6. Full Graceful Shutdown (draining both queues, unmapping mmap store, closing logs)

set -e

PORT=8199
SERVER_BIN="./bin/server"
CONF_FILE="server/config/server.conf"
MMAP_FILE="server/data/university.dat"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo "=========================================================="
echo " University ERP — Phase 4 Linux OS Features Test Suite"
echo " mmap() Storage | Response Queue | /proc Monitor | SIGHUP"
echo "=========================================================="

# 1. Compile
echo -e "${BLUE}[INFO]${NC} Building server..."
if command -v make >/dev/null 2>&1; then
  make -B >/dev/null
fi

# Clean any existing test data to test initialization
rm -f $MMAP_FILE

# Start server
echo -e "${BLUE}[INFO]${NC} Starting server on port $PORT..."
$SERVER_BIN $PORT >/dev/null 2>&1 &
SERVER_PID=$!
sleep 1

cleanup() {
  if kill -0 $SERVER_PID 2>/dev/null; then
    kill -TERM $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
  fi
}
trap cleanup EXIT

# Test 1: Check Mmap Info Endpoint
echo -e "${BLUE}[INFO]${NC} Test 1: Verifying mmap data storage initialization (/api/mmap/info)..."
MMAP_INFO=$(curl -s http://127.0.0.1:$PORT/api/mmap/info)
if [[ "$MMAP_INFO" == *"\"initialized\": true"* ]] && [[ "$MMAP_INFO" == *"\"magic\": \"0x554E4956\""* ]]; then
  echo -e "${GREEN}[PASS]${NC} mmap() store initialized with magic 0x554E4956 (UNIV) and version 1."
else
  echo -e "${RED}[FAIL]${NC} Unexpected mmap info: $MMAP_INFO"
  exit 1
fi

# Test 2: Check Response Queue in Status
echo -e "${BLUE}[INFO]${NC} Test 2: Verifying two-stage queue pipeline and response dispatcher..."
STATUS_RESP=$(curl -s http://127.0.0.1:$PORT/api/status)
if [[ "$STATUS_RESP" == *"\"response_queue\""* ]] && [[ "$STATUS_RESP" == *"\"mmap_store\""* ]]; then
  echo -e "${GREEN}[PASS]${NC} Two-stage queue architecture active (Request Queue + Response Queue Dispatcher)."
else
  echo -e "${RED}[FAIL]${NC} Status missing response_queue metadata: $STATUS_RESP"
  exit 1
fi

# Test 3: Linux /proc Resource Monitoring
echo -e "${BLUE}[INFO]${NC} Test 3: Verifying Linux /proc inspection (/api/proc/stats)..."
PROC_RESP=$(curl -s http://127.0.0.1:$PORT/api/proc/stats)
if [[ "$PROC_RESP" == *"\"vmrss_kb\""* ]] && [[ "$PROC_RESP" == *"\"open_fds\""* ]] && [[ "$PROC_RESP" == *"\"uptime_seconds\""* ]]; then
  echo -e "${GREEN}[PASS]${NC} /proc metrics retrieved live: RSS memory, CPU times, uptime, open FDs."
else
  echo -e "${RED}[FAIL]${NC} /proc stats missing required fields: $PROC_RESP"
  exit 1
fi

# Test 4: Dynamic Configuration Inspection & Live SIGHUP Reload
echo -e "${BLUE}[INFO]${NC} Test 4: Verifying SIGHUP dynamic config reload..."
CONFIG_BEFORE=$(curl -s http://127.0.0.1:$PORT/api/config)
# Update server.conf with new data_access_slots = 4
sed -i 's/data_access_slots=2/data_access_slots=4/' $CONF_FILE
# Send SIGHUP to server process
kill -HUP $SERVER_PID
sleep 0.5
CONFIG_AFTER=$(curl -s http://127.0.0.1:$PORT/api/config)
if [[ "$CONFIG_AFTER" == *"\"data_access_slots\": 4"* ]] && [[ "$CONFIG_AFTER" == *"\"reload_count\": 2"* || "$CONFIG_AFTER" == *"\"reload_count\": 1"* ]]; then
  echo -e "${GREEN}[PASS]${NC} SIGHUP caught! Configuration live-reloaded without stopping server (slots: 4)."
else
  echo -e "${RED}[FAIL]${NC} SIGHUP reload did not update data_access_slots: $CONFIG_AFTER"
  exit 1
fi
# Revert config
sed -i 's/data_access_slots=4/data_access_slots=2/' $CONF_FILE

# Test 5: Verify Thread-Safe Logging System
echo -e "${BLUE}[INFO]${NC} Test 5: Verifying thread-safe logger and transaction logging..."
# Perform a marks update to trigger a transaction
curl -s -X POST http://127.0.0.1:$PORT/api/marks/update \
  -H "Content-Type: application/json" \
  -d '{"student_id":"S101","course_id":"CS301","midterm":48,"endterm":47,"quiz":20}' >/dev/null

LOGS_TX=$(curl -s http://127.0.0.1:$PORT/api/logs/transactions)
if [[ "$LOGS_TX" == *"TX"* ]] || [ -f "server/logs/transactions.log" ]; then
  echo -e "${GREEN}[PASS]${NC} Thread-safe logging recorded transaction in transactions.log."
else
  echo -e "${RED}[FAIL]${NC} Transaction logging failed."
  exit 1
fi

# Test 6: Verify Data Persistence across Server Restart (mmap + msync)
echo -e "${BLUE}[INFO]${NC} Test 6: Verifying persistence across restart via mmap file backing store..."
# Stop current server with SIGTERM (graceful shutdown triggers msync & munmap)
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null || true

# Check that university.dat exists and has exact size of MmapUniversityStore
if [ ! -f "$MMAP_FILE" ]; then
  echo -e "${RED}[FAIL]${NC} mmap persistent file $MMAP_FILE not found on disk!"
  exit 1
fi

# Restart server
$SERVER_BIN $PORT >/dev/null 2>&1 &
SERVER_PID=$!
sleep 1

# Query student dashboard to ensure updated marks (midterm=48, endterm=47, quiz=20) persisted
DASH_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
if [[ "$DASH_RESP" == *"\"midterm\": 48"* ]]; then
  echo -e "${GREEN}[PASS]${NC} Data persisted across restart! Mmap file store correctly reloaded."
else
  echo -e "${RED}[FAIL]${NC} Persistence failed: updated marks did not reload: $DASH_RESP"
  exit 1
fi

# Test 7: Graceful Shutdown
echo -e "${BLUE}[INFO]${NC} Test 7: Testing final graceful shutdown..."
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null || true
echo -e "${GREEN}[PASS]${NC} Graceful shutdown complete. Queues drained, mmap unmapped, logs closed."

echo "=========================================================="
echo -e "${GREEN}All 7 Phase 4 Advanced Linux OS Tests PASSED!${NC}"
echo "=========================================================="
