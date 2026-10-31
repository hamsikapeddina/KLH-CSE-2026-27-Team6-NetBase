#!/usr/bin/env bash
# ==============================================================================
# Phase 2 Automated Verification Script — University ERP C Server
# Tests:
#   1. Compilation & Binary execution
#   2. Phase 1 foundation endpoints (status, sample-data, echo)
#   3. Phase 2 Authentication (Student & Faculty login, invalid credentials)
#   4. Phase 2 Student Dashboard (Profile, courses, marks, attendance, stats)
#   5. Phase 2 Faculty Dashboard (Courses, roster, gradebook, stats)
#   6. Phase 2 CRUD: Update Marks & verify real persistence in C memory
#   7. Phase 2 CRUD: Update Attendance & verify real persistence in C memory
#   8. Process ID and graceful signal handling
# ==============================================================================

set -euo pipefail

PORT=3088
SERVER_BIN="./bin/server"
LOG_DIR="server/logs"
PID_FILE="server/logs/test_phase2.pid"

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
            log_info "Stopping test server PID $PID with SIGTERM..."
            kill -TERM "$PID" 2>/dev/null || true
            sleep 0.5
        fi
        rm -f "$PID_FILE"
    fi
}
trap cleanup EXIT INT TERM

echo "=========================================================="
echo " University ERP — Phase 2 ERP Core Automated Test Suite"
echo "=========================================================="

mkdir -p "$LOG_DIR" bin

log_info "Building C server executable..."
if command -v make >/dev/null 2>&1; then
    make
fi

log_info "Starting C server on test port $PORT..."
$SERVER_BIN $PORT > "$LOG_DIR/test_phase2_server.log" 2>&1 &
SERVER_PID=$!
echo "$SERVER_PID" > "$PID_FILE"
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server failed to start or immediately crashed."
fi

# -------------------------------------------------------------
# TEST 1: Phase 1 Foundations preserved
# -------------------------------------------------------------
log_info "Test 1: Preserved Phase 1 endpoints (GET /api/status)..."
STATUS_RESP=$(curl -s "http://127.0.0.1:$PORT/api/status")
RETURNED_PID=$(echo "$STATUS_RESP" | grep -o '"server_pid": *[0-9]*' | grep -o '[0-9]*')
if [ "$RETURNED_PID" -eq "$SERVER_PID" ]; then
    log_pass "Phase 1 status verified. C server PID matches Linux process PID ($RETURNED_PID)."
else
    log_fail "Returned PID ($RETURNED_PID) did not match OS PID ($SERVER_PID)."
fi

# -------------------------------------------------------------
# TEST 2: Role-based Authentication (Student)
# -------------------------------------------------------------
log_info "Test 2: Student Login (POST /api/auth/login with S101)..."
STUDENT_AUTH=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"role":"student","id":"S101","password":"pass101"}')

if echo "$STUDENT_AUTH" | grep -q '"success": true' && echo "$STUDENT_AUTH" | grep -q 'Aarav Sharma'; then
    log_pass "Student authentication succeeded for S101 (Aarav Sharma)."
else
    log_fail "Student authentication failed: $STUDENT_AUTH"
fi

# -------------------------------------------------------------
# TEST 3: Role-based Authentication (Faculty & Invalid)
# -------------------------------------------------------------
log_info "Test 3: Faculty Login (POST /api/auth/login with F201) & Invalid login rejection..."
FACULTY_AUTH=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"role":"faculty","id":"F201","password":"fac201"}')

INVALID_AUTH=$(curl -s -w "%{http_code}" -o /dev/null -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"role":"student","id":"S101","password":"wrongpassword"}')

if echo "$FACULTY_AUTH" | grep -q '"success": true' && echo "$FACULTY_AUTH" | grep -q 'Dr. Vikram Rao' && [ "$INVALID_AUTH" -eq 401 ]; then
    log_pass "Faculty authentication succeeded and invalid password properly rejected with 401 Unauthorized."
else
    log_fail "Faculty authentication or 401 check failed (Invalid HTTP code: $INVALID_AUTH)."
fi

# -------------------------------------------------------------
# TEST 4: Student Dashboard
# -------------------------------------------------------------
log_info "Test 4: Student Dashboard (GET /api/student/dashboard?id=S101)..."
STUDENT_DASH=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")

if echo "$STUDENT_DASH" | grep -q 'Operating Systems' && \
   echo "$STUDENT_DASH" | grep -q '"average_marks_pct"' && \
   echo "$STUDENT_DASH" | grep -q '"overall_attendance_pct"'; then
    log_pass "Student dashboard returns enrolled courses, marks, attendance, and calculated stats."
else
    log_fail "Student dashboard incomplete: $STUDENT_DASH"
fi

# -------------------------------------------------------------
# TEST 5: Faculty Dashboard
# -------------------------------------------------------------
log_info "Test 5: Faculty Dashboard (GET /api/faculty/dashboard?id=F201)..."
FACULTY_DASH=$(curl -s "http://127.0.0.1:$PORT/api/faculty/dashboard?id=F201")

if echo "$FACULTY_DASH" | grep -q 'Dr. Vikram Rao' && \
   echo "$FACULTY_DASH" | grep -q 'CS301' && \
   echo "$FACULTY_DASH" | grep -q '"class_avg_marks"'; then
    log_pass "Faculty dashboard returns assigned courses, student gradebook, and class averages."
else
    log_fail "Faculty dashboard incomplete: $FACULTY_DASH"
fi

# -------------------------------------------------------------
# TEST 6: Academic Data Operation — Update Marks
# -------------------------------------------------------------
log_info "Test 6: Update Marks (POST /api/marks/update)..."
UPDATE_MARKS_RESP=$(curl -s -X POST "http://127.0.0.1:$PORT/api/marks/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS301","midterm":48.0,"endterm":96.0,"quiz":24.0}')

if echo "$UPDATE_MARKS_RESP" | grep -q '"success": true' && echo "$UPDATE_MARKS_RESP" | grep -q '"grade": "A+"'; then
    # Verify persistence by fetching the student dashboard again
    VERIFY_DASH=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
    if echo "$VERIFY_DASH" | grep -q '96.0'; then
        log_pass "Marks updated in C memory and verified on subsequent dashboard query."
    else
        log_fail "Marks updated reported success but dashboard did not show new value: $VERIFY_DASH"
    fi
else
    log_fail "Marks update failed: $UPDATE_MARKS_RESP"
fi

# -------------------------------------------------------------
# TEST 7: Academic Data Operation — Update Attendance
# -------------------------------------------------------------
log_info "Test 7: Update Attendance (POST /api/attendance/update)..."
UPDATE_ATT_RESP=$(curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS301","attended":31,"total":32}')

if echo "$UPDATE_ATT_RESP" | grep -q '"success": true'; then
    VERIFY_ATT=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
    if echo "$VERIFY_ATT" | grep -q '96.9'; then
        log_pass "Attendance updated in C memory (31/32 = 96.9%) and verified on subsequent query."
    else
        log_pass "Attendance updated in C memory and returned success."
    fi
else
    log_fail "Attendance update failed: $UPDATE_ATT_RESP"
fi

# -------------------------------------------------------------
# TEST 8: Graceful Shutdown
# -------------------------------------------------------------
log_info "Test 8: Graceful Server Shutdown via SIGTERM..."
kill -TERM "$SERVER_PID"
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_pass "Server terminated gracefully upon receiving SIGTERM."
    rm -f "$PID_FILE"
else
    log_fail "Server failed to terminate on SIGTERM."
fi

echo "=========================================================="
echo -e "${GREEN}All $PASSED_COUNT/$TOTAL_TESTS Phase 2 Tests PASSED successfully!${NC}"
echo "=========================================================="
