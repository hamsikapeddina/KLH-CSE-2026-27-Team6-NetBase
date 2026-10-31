#!/usr/bin/env bash
# Phase 1 Automated Test Suite
# Tests POSIX C Server Socket communication and HTTP endpoints

set -e

PORT=8099
SERVER_BIN="./bin/server"

echo "=================================================="
echo " Starting Phase 1 Automated Test Suite"
echo "=================================================="

# 1. Compile Check
echo "[1/7] Building C server..."
if command -v make >/dev/null 2>&1; then
  make clean >/dev/null
  make >/dev/null
fi

if [ ! -x "$SERVER_BIN" ]; then
  echo "FAIL: $SERVER_BIN not found or not executable."
  exit 1
fi
echo "  -> PASS: Binary compiled successfully."

# 2. Launch Server
echo "[2/7] Launching C server on port $PORT..."
$SERVER_BIN $PORT > server/logs/test_run.log 2>&1 &
SERVER_PID=$!
sleep 1

# Ensure server is killed on script exit
cleanup() {
  if kill -0 $SERVER_PID 2>/dev/null; then
    echo "Cleaning up test server process (PID: $SERVER_PID)..."
    kill -TERM $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
  fi
}
trap cleanup EXIT

# 3. Test /api/health
echo "[3/7] Testing GET /api/health..."
HEALTH_RESP=$(curl -s http://127.0.0.1:$PORT/api/health)
if [[ "$HEALTH_RESP" == *"\"status\": \"UP\""* ]]; then
  echo "  -> PASS: Health endpoint returned: $HEALTH_RESP"
else
  echo "  -> FAIL: Unexpected health response: $HEALTH_RESP"
  exit 1
fi

# 4. Test /api/status
echo "[4/7] Testing GET /api/status..."
STATUS_RESP=$(curl -s http://127.0.0.1:$PORT/api/status)
if [[ "$STATUS_RESP" == *"\"status\": \"ONLINE\""* ]] && [[ "$STATUS_RESP" == *"server_pid"* ]]; then
  echo "  -> PASS: Status endpoint returned valid JSON with PID and uptime."
else
  echo "  -> FAIL: Status response malformed: $STATUS_RESP"
  exit 1
fi

# 5. Test /api/sample-data
echo "[5/7] Testing GET /api/sample-data..."
DATA_RESP=$(curl -s http://127.0.0.1:$PORT/api/sample-data)
if [[ "$DATA_RESP" == *"students"* ]] && [[ "$DATA_RESP" == *"Operating Systems"* ]]; then
  echo "  -> PASS: Academic data structures loaded and retrieved correctly."
else
  echo "  -> FAIL: Unexpected data response: $DATA_RESP"
  exit 1
fi

# 6. Test POST /api/echo
echo "[6/7] Testing POST /api/echo..."
ECHO_RESP=$(curl -s -X POST -H "Content-Type: application/json" -d '{"sender":"test_script","message":"hello_posix"}' http://127.0.0.1:$PORT/api/echo)
if [[ "$ECHO_RESP" == *"\"success\": true"* ]] && [[ "$ECHO_RESP" == *"hello_posix"* ]]; then
  echo "  -> PASS: POST payload received and echoed by C server."
else
  echo "  -> FAIL: Echo response malformed: $ECHO_RESP"
  exit 1
fi

# 7. Test Static File Serving
echo "[7/7] Testing static file serving (GET /)..."
INDEX_RESP=$(curl -s http://127.0.0.1:$PORT/)
if [[ "$INDEX_RESP" == *"University ERP"* ]]; then
  echo "  -> PASS: Static web assets served over socket."
else
  echo "  -> FAIL: index.html not served properly."
  exit 1
fi

echo "=================================================="
echo " ALL 7 PHASE 1 TESTS PASSED SUCCESSFULLY!"
echo " Browser -> C Server -> Response Flow Verified!"
echo "=================================================="
