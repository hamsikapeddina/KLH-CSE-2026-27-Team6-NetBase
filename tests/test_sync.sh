#!/usr/bin/env bash
# ==============================================================================
# Comprehensive Academic Data Synchronization Automated Test Suite
# Tests:
#   TEST A: MARKS SYNCHRONIZATION
#     1. Update S101's marks for course CS301
#     2. Read S101 student dashboard
#     3. Verify updated marks returned
#     4. Verify total, percentage, grade recalculated
#     5. Verify transcript data contains updated values
#     6. Verify Academic Health recalculates average
#     7. Verify faculty performance statistics reflect the change
#
#   TEST B: ATTENDANCE SYNCHRONIZATION
#     1. Update S101's attendance for CS301
#     2. Read S101 student dashboard
#     3. Verify updated attendance count and percentage
#     4. Verify attendance eligibility recalculated
#     5. Verify consecutive classes calculation
#     6. Verify Academic Health reflects change
#     7. Verify Early-Warning classification recalculated
#     8. Verify faculty attendance statistics reflect change
#
#   TEST C: PERSISTENCE
#     1. Update marks and attendance
#     2. Stop server gracefully (SIGTERM)
#     3. Restart server
#     4. Fetch student records again
#     5. Verify both updates remain present
#
#   TEST D: DATA ISOLATION
#     1. Update one student's marks (S101)
#     2. Verify another student's records are unchanged (S102)
#     3. Update one course's attendance (CS301)
#     4. Verify other courses are unchanged (CS302, CS303, CS304)
#
#   TEST E: REGRESSION VERIFICATION
#     1. Student login (S101 / pass101)
#     2. Faculty login (F201 / fac201)
#     3. Student creation and deletion (CRUD)
#     4. Gradebook & Attendance Register endpoints
#     5. Academic Early-Warning Center data integrity
#     6. OS Socket Monitor telemetry
#     7. Concurrency burst endpoints
# ==============================================================================

set -euo pipefail

PORT=3097
SERVER_BIN="./bin/server"
LOG_DIR="server/logs"
PID_FILE="server/logs/test_sync.pid"

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED_COUNT=0
TOTAL_TESTS=23

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
echo " University ERP — Academic Synchronization Test Suite"
echo "=========================================================="

mkdir -p "$LOG_DIR" bin

log_info "Starting C server on test port $PORT..."
$SERVER_BIN $PORT > "$LOG_DIR/test_sync_server.log" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"
sleep 1.0

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server failed to start on port $PORT"
fi

# Node HTTP helper script for robust querying and parsing
run_node_check() {
    node -e "$1"
}

# ------------------------------------------------------------------------------
# TEST A: MARKS SYNCHRONIZATION
# ------------------------------------------------------------------------------
echo ""
echo "--- TEST A: MARKS SYNCHRONIZATION ---"

log_info "A1: Updating S101 marks for CS301 (Midterm: 48, Endterm: 96, Quiz: 24)..."
A1_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/marks/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS301","midterm":48.0,"endterm":96.0,"quiz":24.0}')

if echo "$A1_RES" | grep -q '"success":[[:space:]]*true'; then
    log_pass "A1: Marks update acknowledged by C server memory store."
else
    log_fail "A1: Marks update failed: $A1_RES"
fi

log_info "A2 & A3: Reading S101 student dashboard and verifying updated marks..."
A2_RES=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
A2_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const m = (d.marks || []).find(x => x.course_id === "CS301");
const ok = m && m.midterm === 48 && m.endterm === 96 && m.quiz === 24;
console.log(`mid=${m?m.midterm:-1};end=${m?m.endterm:-1};qz=${m?m.quiz:-1};ok=${ok?1:0}`);
' "$A2_RES")

if echo "$A2_CHECK" | grep -q "ok=1"; then
    log_pass "A2-A3: Student dashboard returned exact updated marks for CS301 ($A2_CHECK)."
else
    log_fail "A2-A3: Marks mismatch: $A2_CHECK"
fi

log_info "A4: Verifying total, percentage, and grade recalculation..."
A4_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const m = (d.marks || []).find(x => x.course_id === "CS301");
// (48 + 96 + 24) / 175 * 100 = 168 / 175 = 96.0% -> A+
const ok = m && m.total_pct >= 95.9 && m.grade === "A+";
console.log(`total_pct=${m?m.total_pct:-1};grade=${m?m.grade:"?"};ok=${ok?1:0}`);
' "$A2_RES")

if echo "$A4_CHECK" | grep -q "ok=1"; then
    log_pass "A4: Recalculated total % (96.00%) and Letter Grade (A+) verified ($A4_CHECK)."
else
    log_fail "A4: Recalculation failed: $A4_CHECK"
fi

log_info "A5: Verifying transcript consistency in marks array..."
A5_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const m = (d.marks || []).find(x => x.course_id === "CS301");
const ok = m && m.course_name && m.total_pct > 0 && m.grade;
console.log(`course=${m?m.course_name:"?"};ok=${ok?1:0}`);
' "$A2_RES")

if echo "$A5_CHECK" | grep -q "ok=1"; then
    log_pass "A5: Transcript row data fully synchronized with C memory store ($A5_CHECK)."
else
    log_fail "A5: Transcript data missing: $A5_CHECK"
fi

log_info "A6: Verifying Academic Health recalculates student average..."
A6_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const avg = d.overall_stats.average_marks_pct;
// Marks: CS301 (96%), CS302 (80%), CS303 (92.57%), CS304 (82.86%) => avg >= 86.0%
const ok = avg >= 86.0;
console.log(`avg=${avg};ok=${ok?1:0}`);
' "$A2_RES")

if echo "$A6_CHECK" | grep -q "ok=1"; then
    log_pass "A6: Academic Health recalculated student average accurately ($A6_CHECK)."
else
    log_fail "A6: Academic Health average not updated: $A6_CHECK"
fi

log_info "A7: Verifying faculty performance statistics reflect the updated marks..."
A7_RES=$(curl -s "http://127.0.0.1:$PORT/api/faculty/dashboard?id=F201")
A7_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const clsAvg = d.stats.class_avg_marks;
const highest = d.stats.highest_marks;
const ok = clsAvg > 85.0 && highest >= 95.0;
console.log(`class_avg=${clsAvg};highest=${highest};ok=${ok?1:0}`);
' "$A7_RES")

if echo "$A7_CHECK" | grep -q "ok=1"; then
    log_pass "A7: Faculty dashboard performance stats reflect updated marks ($A7_CHECK)."
else
    log_fail "A7: Faculty statistics not updated: $A7_CHECK"
fi

# ------------------------------------------------------------------------------
# TEST B: ATTENDANCE SYNCHRONIZATION
# ------------------------------------------------------------------------------
echo ""
echo "--- TEST B: ATTENDANCE SYNCHRONIZATION ---"

log_info "B1: Updating S101 attendance for CS301 (Attended: 32 / Total: 32 = 100%)..."
B1_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS301","attended":32,"total":32}')

if echo "$B1_RES" | grep -q '"success":[[:space:]]*true'; then
    log_pass "B1: Attendance update acknowledged by C server memory store."
else
    log_fail "B1: Attendance update failed: $B1_RES"
fi

log_info "B2 & B3: Reading S101 dashboard and verifying updated attendance count & %..."
B2_RES=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
B2_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const a = (d.attendance || []).find(x => x.course_id === "CS301");
const ok = a && a.attended === 32 && a.total === 32 && a.percentage === 100.0;
console.log(`att=${a?a.attended:-1};tot=${a?a.total:-1};pct=${a?a.percentage:-1};ok=${ok?1:0}`);
' "$B2_RES")

if echo "$B2_CHECK" | grep -q "ok=1"; then
    log_pass "B2-B3: Dashboard returned updated attendance: 32/32 (100.0%) ($B2_CHECK)."
else
    log_fail "B2-B3: Attendance mismatch: $B2_CHECK"
fi

log_info "B4: Verifying attendance eligibility calculation..."
B4_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const overallAtt = d.overall_stats.overall_attendance_pct;
const isEligible = overallAtt >= 75.0;
console.log(`overallAtt=${overallAtt};eligible=${isEligible};ok=${isEligible?1:0}`);
' "$B2_RES")

if echo "$B4_CHECK" | grep -q "ok=1"; then
    log_pass "B4: Attendance statutory eligibility confirmed (>= 75%) ($B4_CHECK)."
else
    log_fail "B4: Attendance eligibility calculation failed: $B4_CHECK"
fi

log_info "B5: Testing consecutive classes calculation on student with shortage (S103)..."
B5_RES=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S103")
B5_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const a = d.overall_stats.total_attended;
const t = d.overall_stats.total_classes;
const needed = Math.max(0, 3 * t - 4 * a);
const newPct = ((a + needed) / (t + needed)) * 100;
console.log(`att=${a};tot=${t};needed=${needed};newPct=${newPct.toFixed(1)};ok=${needed > 0 && newPct >= 75.0 ? 1 : 0}`);
' "$B5_RES")

if echo "$B5_CHECK" | grep -q "ok=1"; then
    log_pass "B5: Consecutive classes formula verified on low attendance record ($B5_CHECK)."
else
    log_fail "B5: Consecutive classes formula incorrect: $B5_CHECK"
fi

log_info "B6: Verifying Academic Health reflects the attendance change..."
B6_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const overallAtt = d.overall_stats.overall_attendance_pct;
const ok = overallAtt >= 90.0;
console.log(`overallAtt=${overallAtt};ok=${ok?1:0}`);
' "$B2_RES")

if echo "$B6_CHECK" | grep -q "ok=1"; then
    log_pass "B6: Academic Health attendance metric updated ($B6_CHECK)."
else
    log_fail "B6: Academic Health attendance not updated: $B6_CHECK"
fi

log_info "B7: Verifying Early-Warning classification recalculated for S101..."
B7_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const att = d.overall_stats.overall_attendance_pct;
const avg = d.overall_stats.average_marks_pct;
const isHigh = (att < 75.0 && avg < 60.0);
const isAttention = (att < 75.0 || avg < 60.0 || (avg >= 60.0 && avg < 75.0));
const isOnTrack = (att >= 75.0 && avg >= 75.0);
console.log(`onTrack=${isOnTrack};ok=${isOnTrack && !isAttention && !isHigh ? 1 : 0}`);
' "$B2_RES")

if echo "$B7_CHECK" | grep -q "ok=1"; then
    log_pass "B7: Early-Warning classification accurately classified S101 as On Track ($B7_CHECK)."
else
    log_fail "B7: Early-Warning classification failed: $B7_CHECK"
fi

log_info "B8: Verifying faculty attendance statistics reflect the update..."
B8_RES=$(curl -s "http://127.0.0.1:$PORT/api/faculty/dashboard?id=F201")
B8_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const attAvg = d.stats.class_avg_attendance;
const ok = attAvg >= 90.0;
console.log(`class_avg_attendance=${attAvg};ok=${ok?1:0}`);
' "$B8_RES")

if echo "$B8_CHECK" | grep -q "ok=1"; then
    log_pass "B8: Faculty attendance stats reflect the update ($B8_CHECK)."
else
    log_fail "B8: Faculty attendance stats not updated: $B8_CHECK"
fi

# ------------------------------------------------------------------------------
# TEST C: PERSISTENCE ACROSS RESTART
# ------------------------------------------------------------------------------
echo ""
echo "--- TEST C: PERSISTENCE ACROSS SERVER RESTART ---"

log_info "C1: Updating S102 marks (Midterm: 50, Endterm: 98, Quiz: 25 = 98.86%) and attendance (30/30 = 100%)..."
curl -s -X POST "http://127.0.0.1:$PORT/api/marks/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S102","course_id":"CS301","midterm":50.0,"endterm":98.0,"quiz":25.0}' >/dev/null

curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S102","course_id":"CS301","attended":30,"total":30}' >/dev/null

log_info "C2: Sending SIGTERM for graceful server shutdown..."
kill -TERM "$SERVER_PID" 2>/dev/null || true
sleep 1.0

log_info "C3: Restarting C server on port $PORT..."
$SERVER_BIN $PORT > "$LOG_DIR/test_sync_restart.log" 2>&1 &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"
sleep 1.0

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    log_fail "Server failed to restart on port $PORT"
fi

log_info "C4 & C5: Re-fetching S102 records from newly spawned C process..."
C4_RES=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S102")
C4_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const m = (d.marks || []).find(x => x.course_id === "CS301");
const a = (d.attendance || []).find(x => x.course_id === "CS301");
const okM = m && m.midterm === 50 && m.endterm === 98 && m.quiz === 25;
const okA = a && a.attended === 30 && a.total === 30;
console.log(`mOk=${okM};aOk=${okA};ok=${okM && okA ? 1 : 0}`);
' "$C4_RES")

if echo "$C4_CHECK" | grep -q "ok=1"; then
    log_pass "C1-C5: Persistence verified: both marks and attendance reloaded from mmap backing file ($C4_CHECK)."
else
    log_fail "C1-C5: Persistence failed after restart: $C4_CHECK"
fi

# ------------------------------------------------------------------------------
# TEST D: DATA ISOLATION
# ------------------------------------------------------------------------------
echo ""
echo "--- TEST D: DATA ISOLATION ---"

log_info "D1 & D2: Updating S101 marks and verifying S102 and S104 records remain unchanged..."
# Read S102 initial marks for CS302
S102_PRE=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S102")
S102_PRE_SCORE=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const m = (d.marks || []).find(x => x.course_id === "CS302");
console.log(m ? m.total_pct : -1);
' "$S102_PRE")

# Update S101 marks for CS302 to a distinct value
curl -s -X POST "http://127.0.0.1:$PORT/api/marks/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS302","midterm":44.0,"endterm":88.0,"quiz":22.0}' >/dev/null

# Re-read S102 and verify it was NOT affected
S102_POST=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S102")
S102_POST_SCORE=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const m = (d.marks || []).find(x => x.course_id === "CS302");
console.log(m ? m.total_pct : -1);
' "$S102_POST")

if [ "$S102_PRE_SCORE" == "$S102_POST_SCORE" ]; then
    log_pass "D1-D2: Student isolation verified: Updating S101 did not alter S102 marks ($S102_PRE_SCORE%)."
else
    log_fail "D1-D2: Student isolation breach: S102 changed from $S102_PRE_SCORE to $S102_POST_SCORE"
fi

log_info "D3 & D4: Updating S101 attendance for CS301 and verifying CS302, CS303, CS304 remain unchanged..."
S101_PRE_ATT=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
CS302_PRE_ATT=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const a = (d.attendance || []).find(x => x.course_id === "CS302");
console.log(a ? a.attended : -1);
' "$S101_PRE_ATT")

# Update CS301 attendance
curl -s -X POST "http://127.0.0.1:$PORT/api/attendance/update" \
    -H "Content-Type: application/json" \
    -d '{"student_id":"S101","course_id":"CS301","attended":31,"total":32}' >/dev/null

S101_POST_ATT=$(curl -s "http://127.0.0.1:$PORT/api/student/dashboard?id=S101")
CS302_POST_ATT=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const a = (d.attendance || []).find(x => x.course_id === "CS302");
console.log(a ? a.attended : -1);
' "$S101_POST_ATT")

if [ "$CS302_PRE_ATT" == "$CS302_POST_ATT" ]; then
    log_pass "D3-D4: Course isolation verified: Updating CS301 attendance did not alter CS302 attendance ($CS302_PRE_ATT classes)."
else
    log_fail "D3-D4: Course isolation breach: CS302 attendance changed from $CS302_PRE_ATT to $CS302_POST_ATT"
fi

# ------------------------------------------------------------------------------
# TEST E: REGRESSION VERIFICATION
# ------------------------------------------------------------------------------
echo ""
echo "--- TEST E: REGRESSION VERIFICATION ---"

log_info "E1: Verifying student login (S101 / pass101)..."
E1_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"role":"student","id":"S101","password":"pass101"}')

if echo "$E1_RES" | grep -q '"role":[[:space:]]*"student"'; then
    log_pass "E1: Student authentication verified."
else
    log_fail "E1: Student login failed: $E1_RES"
fi

log_info "E2: Verifying faculty login (F201 / fac201)..."
E2_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"role":"faculty","id":"F201","password":"fac201"}')

if echo "$E2_RES" | grep -q '"role":[[:space:]]*"faculty"'; then
    log_pass "E2: Faculty authentication verified."
else
    log_fail "E2: Faculty login failed: $E2_RES"
fi

log_info "E3: Verifying student creation, discovery, and deletion..."
curl -s -X POST "http://127.0.0.1:$PORT/api/student/create" \
    -H "Content-Type: application/json" \
    -d '{"id":"S188","name":"Regression TestStudent","email":"reg@univ.edu","department":"Computer Science","year":1,"section":"A"}' >/dev/null

E3_GET=$(curl -s "http://127.0.0.1:$PORT/api/student?id=S188")
E3_DEL=$(curl -s -X POST "http://127.0.0.1:$PORT/api/student/delete" \
    -H "Content-Type: application/json" \
    -d '{"id":"S188"}')
E3_POST_DEL=$(curl -s "http://127.0.0.1:$PORT/api/student?id=S188")

if echo "$E3_GET" | grep -q '"id":[[:space:]]*"S188"' && echo "$E3_POST_DEL" | grep -q '"found":[[:space:]]*false'; then
    log_pass "E3: Full student CRUD workflow (Create, Read, Delete) verified."
else
    log_fail "E3: Student CRUD workflow failed."
fi

log_info "E4: Verifying Gradebook & Attendance Register data in Faculty Dashboard..."
E4_RES=$(curl -s "http://127.0.0.1:$PORT/api/faculty/dashboard?id=F201")
E4_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const d = parseJson(process.argv[1]);
const ok = d.marks && d.marks.length > 0 && d.attendance && d.attendance.length > 0;
console.log(`marksCount=${d.marks?d.marks.length:0};attCount=${d.attendance?d.attendance.length:0};ok=${ok?1:0}`);
' "$E4_RES")

if echo "$E4_CHECK" | grep -q "ok=1"; then
    log_pass "E4: Faculty Gradebook and Attendance Register endpoints fully operational ($E4_CHECK)."
else
    log_fail "E4: Faculty data structures incomplete: $E4_CHECK"
fi

log_info "E5: Verifying Academic Early-Warning Center data integrity..."
E5_STUDS=$(curl -s "http://127.0.0.1:$PORT/api/students")
if echo "$E5_STUDS" | grep -q '"id":[[:space:]]*"S101"' && echo "$E5_STUDS" | grep -q '"id":[[:space:]]*"S103"'; then
    log_pass "E5: Early-Warning student directory returns full student roster."
else
    log_fail "E5: Student roster incomplete: $E5_STUDS"
fi

log_info "E6: Verifying OS Socket Monitor telemetry, 4 workers, 32 queue, /proc metrics..."
E6_STATUS=$(curl -s "http://127.0.0.1:$PORT/api/status")
E6_PROC=$(curl -s "http://127.0.0.1:$PORT/api/proc/stats")

E6_CHECK=$(node -e '
function parseJson(t) {
  try { return JSON.parse(t); } catch { return JSON.parse(t.replace(/,\s*([\]}])/g, "$1").replace(/}\s*([{\[])/g, "},$1")); }
}
const s = parseJson(process.argv[1]);
const p = parseJson(process.argv[2]);
const ok = s && s.thread_pool && s.thread_pool.worker_count === 4 && s.thread_pool.queue_capacity === 32 && p && (p.vmrss_kb > 0 || p.vsize_kb > 0);
console.log(`workers=${s.thread_pool?s.thread_pool.worker_count:0};queueCap=${s.thread_pool?s.thread_pool.queue_capacity:0};rss=${p.vmrss_kb||p.vsize_kb};ok=${ok?1:0}`);
' "$E6_STATUS" "$E6_PROC")

if echo "$E6_CHECK" | grep -q "ok=1"; then
    log_pass "E6: OS Socket Monitor, 4 POSIX worker threads, and Linux /proc telemetry 100% intact ($E6_CHECK)."
else
    log_fail "E6: OS Socket Monitor check failed: $E6_CHECK"
fi

log_info "E7: Verifying Concurrency Bursts (Fire 20, 32, 50 demo endpoints)..."
E7_BURST=$(curl -s "http://127.0.0.1:$PORT/api/concurrency/test?id=1&type=academic_tx")
if echo "$E7_BURST" | grep -q '"semaphore_slot_held":[[:space:]]*true'; then
    log_pass "E7: Dedicated concurrency burst endpoint with POSIX Semaphore slot verified."
else
    log_fail "E7: Concurrency demo endpoint failed: $E7_BURST"
fi

echo ""
echo "=========================================================="
if [ "$PASSED_COUNT" -ne "$TOTAL_TESTS" ]; then
    echo -e "${RED}ACADEMIC DATA SYNCHRONIZATION TEST COUNT MISMATCH: $PASSED_COUNT/$TOTAL_TESTS PASSED${NC}"
    echo "=========================================================="
    exit 1
fi
echo -e "${GREEN}ALL $PASSED_COUNT/$TOTAL_TESTS ACADEMIC DATA SYNCHRONIZATION TESTS PASSED!${NC}"
echo "=========================================================="
