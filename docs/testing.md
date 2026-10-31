# Testing Guide — Phase 1: Foundation

## 1. Automated Test Script

The test script `tests/test_phase1.sh` tests server compilation, startup, endpoints, and graceful shutdown.

### Running the Test
```bash
make test
```
Or directly:
```bash
bash tests/test_phase1.sh
```

---

## 2. Test Cases Matrix

| Test ID | Objective | Command / Method | Expected Result | Status |
|---|---|---|---|---|
| **TC-01** | Build Verification | `make clean && make` | Binaries compiled into `bin/server` with 0 warnings | PASS |
| **TC-02** | Socket Binding | `./bin/server 3000 &` | Server listens on `0.0.0.0:3000` | PASS |
| **TC-03** | Server Health API | `curl -s http://127.0.0.1:3000/api/health` | Returns `{"status":"UP","phase":1}` | PASS |
| **TC-04** | Status & PID API | `curl -s http://127.0.0.1:3000/api/status` | Returns valid JSON with PID, uptime, total requests | PASS |
| **TC-05** | Academic Data API | `curl -s http://127.0.0.1:3000/api/sample-data` | Returns Students, Faculty, Courses arrays | PASS |
| **TC-06** | Browser POST Echo | `curl -X POST -d '{"msg":"test"}' .../api/echo` | C server receives payload and echoes it back | PASS |
| **TC-07** | Static File Serving | `curl -s http://127.0.0.1:3000/` | Returns `web/index.html` with `text/html` | PASS |
| **TC-08** | Security Check | `curl -s http://127.0.0.1:3000/../etc/passwd` | Returns HTTP 403 Path Traversal Blocked | PASS |
| **TC-09** | Signal Termination | `kill -TERM <PID>` | Graceful shutdown, socket closed, exit 0 | PASS |
