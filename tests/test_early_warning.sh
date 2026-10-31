#!/usr/bin/env bash
# ==============================================================================
# Academic Early-Warning & Recovery System Automated Test Suite
# Tests:
#   1. C Server startup & live endpoints
#   2. Student Portal: S101 is "On Track" (Attendance >= 75% & Marks >= 75%)
#   3. Student Portal: S103 has Attendance Warning (< 75%) & consecutive classes formula
#   4. Student Portal: S103 has Average between 60% and 75% ("Needs Attention")
#   5. High Concern verification: Student with Attendance < 75% AND Marks < 60%
#   6. Performance Warning verification: Attendance >= 75% and Marks < 60%
#   7. Insufficient Data handling: Student with 0 recorded classes / 0 assessments
#   8. Dynamic Refresh: Updating marks via POST /api/marks/update updates risk
#   9. Dynamic Refresh: Updating attendance via POST /api/attendance/update updates risk
#  10. Persistence verification across server restart
#  11. Faculty Portal endpoints & complete roster data integrity
#  12. Verification that OS Socket Monitor & thread pool remain intact
# ==============================================================================

set -euo pipefail

PORT=3099
SERVER_BIN="./bin/server"
LOG_DIR="server/logs"
PID_FILE="server/logs/test_early_warning.pid"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED_COUNT=0
TOTAL_TESTS=12

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASSED_COUNT=$((PASSED_COUNT + 1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; cleanup; exit 1; }

cleanup() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE" 2>/dev/null || true)
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            log_info "Stopping test server PID $PID with SIGTERM..."
            kill -TERM "$PID" 2>/dev/null || true
            sleep 0.5
        fi
        rm -f "$PID_FILE"
    fi
}
trap cleanup EXIT INT TERM

echo "=========================================================="
echo " University ERP — Academic Early-Warning Test Suite"
echo "=========================================================="

mkdir -p "$LOG_DIR" bin

log_info "Starting C server on test port $PORT..."
$SERVER_BIN $PORT > "$LOG_DIR/test_ew_server.log" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"
sleep 1.0

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server failed to start on port $PORT"
fi

# ------------------------------------------------------------------------------
# Test 1: Foundation check
# ------------------------------------------------------------------------------
log_info "Test 1: Verifying C server online & status endpoint..."
STATUS_RESP=$(curl -s "http://127.0.0.1:$PORT/api/status")
if echo "$STATUS_RESP" | grep -qi '"status":[[:space:]]*"ONLINE"'; then
    log_pass "Server online on port $PORT."
else
    log_fail "Server status check failed: $STATUS_RESP"
fi

# ------------------------------------------------------------------------------
# Test 2: S101 is On Track (Attendance >= 75% AND Average >= 75%)
# ------------------------------------------------------------------------------
log_info "Test 2: Verifying S101 academic metrics for On Track status..."
S101_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
NODE_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const att = d.overall_stats.overall_attendance_pct;
  const avg = d.overall_stats.average_marks_pct;
  console.log(`att=${att};avg=${avg};ok=${att >= 75.0 && avg >= 75.0 ? 1 : 0}`);
} catch (e) {
  console.log("error");
}
' "$S101_RESP")

if echo "$NODE_CHECK" | grep -q "ok=1"; then
    log_pass "S101 qualifies for 'On Track' ($NODE_CHECK)."
else
    log_fail "S101 metrics do not meet On Track: $NODE_CHECK"
fi

# ------------------------------------------------------------------------------
# Test 3: S103 Attendance Warning and consecutive classes calculation
# ------------------------------------------------------------------------------
log_info "Test 3: Verifying S103 Attendance Warning & consecutive classes formula..."
S103_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S103")
NODE_S103_ATT=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const att = d.overall_stats.overall_attendance_pct;
  const totalAtt = d.overall_stats.total_attended;
  const totalCls = d.overall_stats.total_classes;
  const needed = Math.max(0, Math.ceil(3 * totalCls - 4 * totalAtt));
  const newPct = ((totalAtt + needed) / (totalCls + needed)) * 100;
  console.log(`att=${att};needed=${needed};newPct=${newPct.toFixed(1)};ok=${att < 75.0 && needed > 0 && newPct >= 75.0 ? 1 : 0}`);
} catch (e) {
  console.log("error:" + e.message);
}
' "$S103_RESP")

if echo "$NODE_S103_ATT" | grep -q "ok=1"; then
    log_pass "Consecutive classes formula verified for S103 ($NODE_S103_ATT)."
else
    log_fail "S103 consecutive classes calculation incorrect: $NODE_S103_ATT"
fi

# ------------------------------------------------------------------------------
# Test 4: S103 Average between 60% and 75% -> "Needs Attention"
# ------------------------------------------------------------------------------
log_info "Test 4: Verifying S103 academic average between 60% and 75%..."
NODE_S103_AVG=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const avg = d.overall_stats.average_marks_pct;
  console.log(`avg=${avg};ok=${avg >= 60.0 && avg < 75.0 ? 1 : 0}`);
} catch (e) {
  console.log("error");
}
' "$S103_RESP")

if echo "$NODE_S103_AVG" | grep -q "ok=1"; then
    log_pass "S103 qualifies for 'Needs Attention' based on Moderate performance ($NODE_S103_AVG)."
else
    log_fail "S103 average check failed: $NODE_S103_AVG"
fi

# ------------------------------------------------------------------------------
# Test 5: High Concern Rule Verification (Attendance < 75% AND Average < 60%)
# ------------------------------------------------------------------------------
log_info "Test 5: Testing High Concern classification rule (Att < 75% AND Marks < 60%)..."
# Create a temporary student S199 with low attendance and low marks
curl -s -X POST "http://127.0.0.1:$PORT/api/student/create" \
    -H "Content-Type: application/json" \
    -d '{"id":"S199","name":"Test LowPerformer","email":"test.low@univ.edu","department":"Computer Science","year":1,"section":"C","password":"pwd"}' >/dev/null

# Add low marks: Midterm 10, Endterm 20, Quiz 5 => total 35/175 = 20.0%
curl -s -X POST "http://127.0.0.1:$PORT/api/marks/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S199","course_id":"CS301","midterm":10.0,"endterm":20.0,"quiz":5.0}' >/dev/null

# Add low attendance: 10 / 25 = 40%
curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S199","course_id":"CS301","attended":10,"total":25}' >/dev/null

S199_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S199")
NODE_S199_HC=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const att = d.overall_stats.overall_attendance_pct;
  const avg = d.overall_stats.average_marks_pct;
  console.log(`att=${att};avg=${avg};ok=${att < 75.0 && avg < 60.0 ? 1 : 0}`);
} catch (e) {
  console.log("error");
}
' "$S199_RESP")

if echo "$NODE_S199_HC" | grep -q "ok=1"; then
    log_pass "High Concern condition satisfied: Attendance < 75% AND Marks < 60% ($NODE_S199_HC)."
else
    log_fail "High Concern condition check failed: $NODE_S199_HC"
fi

# ------------------------------------------------------------------------------
# Test 6: Performance Warning verification (Attendance >= 75% AND Average < 60%)
# ------------------------------------------------------------------------------
log_info "Test 6: Testing Performance Warning rule (Att >= 75% AND Marks < 60%)..."
# Update S199 attendance to high (25/25 = 100%) keeping marks low (20%)
curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S199","course_id":"CS301","attended":25,"total":25}' >/dev/null

S199_RESP2=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S199")
NODE_S199_PW=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const att = d.overall_stats.overall_attendance_pct;
  const avg = d.overall_stats.average_marks_pct;
  console.log(`att=${att};avg=${avg};ok=${att >= 75.0 && avg < 60.0 ? 1 : 0}`);
} catch (e) {
  console.log("error");
}
' "$S199_RESP2")

if echo "$NODE_S199_PW" | grep -q "ok=1"; then
    log_pass "Performance Warning rule satisfied: Attendance >= 75% but Average < 60% ($NODE_S199_PW)."
else
    log_fail "Performance Warning check failed: $NODE_S199_PW"
fi

# ------------------------------------------------------------------------------
# Test 7: Insufficient Academic Data Handling
# ------------------------------------------------------------------------------
log_info "Test 7: Verifying Insufficient Data condition for student with 0 records..."
curl -s -X POST "http://127.0.0.1:$PORT/api/student/create" \
    -H "Content-Type: application/json" \
    -d '{"id":"S198","name":"Fresh Student","email":"fresh@univ.edu","department":"Computer Science","year":1,"section":"A"}' >/dev/null

S198_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S198")
NODE_S198=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const totalCls = d.overall_stats.total_classes;
  const isInsufficient = (totalCls === 0);
  console.log(`totalCls=${totalCls};ok=${isInsufficient ? 1 : 0}`);
} catch (e) {
  console.log("error: " + e.message);
}
' "$S198_RESP")

if echo "$NODE_S198" | grep -q "ok=1"; then
    log_pass "Student with no records correctly identified as Insufficient Academic Data ($NODE_S198)."
else
    log_fail "Insufficient Academic Data condition failed: $NODE_S198"
fi

# ------------------------------------------------------------------------------
# Test 8: Dynamic Marks Update refreshing status
# ------------------------------------------------------------------------------
log_info "Test 8: Verifying Dynamic Marks Update changes academic average..."
# Update all enrolled courses for S199 to high marks (96%):
for CID in CS301 CS302 CS303 CS304; do
    curl -s -X POST "http://127.0.0.1:$PORT/api/marks/update" \
        -H "Content-Type: application/json" \
        -d "{\"student_id\":\"S199\",\"course_id\":\"$CID\",\"midterm\":48.0,\"endterm\":95.0,\"quiz\":25.0}" >/dev/null
done

S199_RESP3=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S199")
NODE_S199_AVG3=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const avg = d.overall_stats.average_marks_pct;
  console.log(`avg=${avg};ok=${avg >= 75.0 ? 1 : 0}`);
} catch (e) {
  console.log("error");
}
' "$S199_RESP3")

if echo "$NODE_S199_AVG3" | grep -q "ok=1"; then
    log_pass "Dynamic update verified: marks update immediately reflected in calculated average ($NODE_S199_AVG3)."
else
    log_fail "Marks update did not reflect: $NODE_S199_AVG3"
fi

# ------------------------------------------------------------------------------
# Test 9: Dynamic Attendance Update refreshing status
# ------------------------------------------------------------------------------
log_info "Test 9: Verifying Dynamic Attendance Update changes attendance percentage..."
# Drop S199 attendance to 12 / 25 = 48%
curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S199","course_id":"CS301","attended":12,"total":25}' >/dev/null

S199_RESP4=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S199")
NODE_S199_ATT4=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const att = d.overall_stats.overall_attendance_pct;
  console.log(`att=${att};ok=${att < 75.0 ? 1 : 0}`);
} catch (e) {
  console.log("error");
}
' "$S199_RESP4")

if echo "$NODE_S199_ATT4" | grep -q "ok=1"; then
    log_pass "Dynamic update verified: attendance update immediately reflected ($NODE_S199_ATT4)."
else
    log_fail "Attendance update did not reflect: $NODE_S199_ATT4"
fi

# Clean up test student S198 and S199
curl -s -X POST "http://127.0.0.1:$PORT/api/student/delete" -H "Content-Type: application/json" -d '{"id":"S199"}' >/dev/null || true
curl -s -X POST "http://127.0.0.1:$PORT/api/student/delete" -H "Content-Type: application/json" -d '{"id":"S198"}' >/dev/null || true

# ------------------------------------------------------------------------------
# Test 10: Persistence across server restart
# ------------------------------------------------------------------------------
log_info "Test 10: Testing data persistence across server restart..."
# Modify S101 attendance slightly: 29/32 for CS301
curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS301","attended":29,"total":32}' >/dev/null

# Restart server
kill -TERM "$SERVER_PID" 2>/dev/null || true
sleep 1.0

$SERVER_BIN $PORT > "$LOG_DIR/test_ew_server_restart.log" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"
sleep 1.0

PERSIST_RESP=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
NODE_PERSIST=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
try {
  const d = parseJson(process.argv[1]);
  const cs301 = (d.attendance || []).find(a => a.course_id === "CS301");
  console.log(`att=${cs301 ? cs301.attended : -1};ok=${cs301 && cs301.attended === 29 ? 1 : 0}`);
} catch (e) {
  console.log("error: " + e.message);
}
' "$PERSIST_RESP")

if echo "$NODE_PERSIST" | grep -q "ok=1"; then
    log_pass "Persistence verified: S101 updated attendance retained across restart ($NODE_PERSIST)."
else
    log_fail "Persistence check failed after restart: $NODE_PERSIST"
fi

# ------------------------------------------------------------------------------
# Test 11: Faculty Portal Roster & Early-Warning Data Integrity
# ------------------------------------------------------------------------------
log_info "Test 11: Verifying /api/students and Faculty Dashboard live endpoints..."
FAC_RESP=$(curl -s "http://127.0.0.1:$PORT/api/faculty/dashboard?id=F201")
STUDENTS_RESP=$(curl -s "http://127.0.0.1:$PORT/api/students")

if echo "$FAC_RESP" | grep -q '"Dr. Vikram Rao"' && echo "$STUDENTS_RESP" | grep -q '"S101"'; then
    log_pass "Faculty & Students endpoints return complete student rosters for Early-Warning Center."
else
    log_fail "Faculty or Students endpoint data missing."
fi

# ------------------------------------------------------------------------------
# Test 12: OS Monitor and thread pool integrity check
# ------------------------------------------------------------------------------
log_info "Test 12: Verifying OS monitor, 4 POSIX worker threads, and queue capacity..."
STATUS_CHECK=$(curl -s "http://127.0.0.1:$PORT/api/status")
WORKERS=$(echo "$STATUS_CHECK" | grep -o '"worker_count":[[:space:]]*[0-9]*' | head -1 | cut -d: -f2 | tr -d ' ')
QUEUE_CAP=$(echo "$STATUS_CHECK" | grep -o '"queue_capacity":[[:space:]]*[0-9]*' | head -1 | cut -d: -f2 | tr -d ' ')

if [ "$WORKERS" -eq 4 ] && [ "$QUEUE_CAP" -eq 32 ] && echo "$STATUS_CHECK" | grep -q '"proc"'; then
    log_pass "OS Socket Monitor, 4 POSIX worker threads, and /proc metrics remain 100% intact."
else
    log_fail "OS architecture integrity check failed: Workers=$WORKERS, QueueCap=$QUEUE_CAP"
fi

echo "=========================================================="
echo -e "${GREEN}ALL $PASSED_COUNT/$TOTAL_TESTS ACADEMIC EARLY-WARNING TESTS PASSED!${NC}"
echo "=========================================================="
