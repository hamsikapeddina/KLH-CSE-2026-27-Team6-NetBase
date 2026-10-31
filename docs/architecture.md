# Complete System Architecture — University ERP

## 1. System Overview

A Linux-based concurrent University ERP implemented using a C client-server architecture. Multiple student and faculty clients can access academic data concurrently. POSIX threads, mutexes, condition variables, semaphores, memory-mapped files, Linux signals, `/proc`, POSIX sockets, and logging provide safe concurrent access and server monitoring.

This system is deliberately engineered to demonstrate fundamental Operating Systems principles in action rather than using high-level web frameworks or relational database management systems.

### Data Layer
The data layer is **a simple persistent file-backed academic data store implemented using POSIX memory mapping (`mmap`)**. It does not use a relational DBMS (such as MySQL or PostgreSQL) or an embedded SQL engine (such as SQLite); instead, records are laid out directly in a structured binary file (`university.dat`) that is mapped into process virtual memory, with changes committed via `msync()`.

---

## 2. End-to-End Request / Response Pipeline

```text
Browser Client (Vanilla HTML5 / Modern CSS / ES6 JavaScript)
        │
        │ HTTP Requests (GET, POST over TCP stream)
        ▼
[1. POSIX Socket Layer]
    AF_INET, SOCK_STREAM, SO_REUSEADDR (port 3000)
        │
        │ accept() yields client_fd
        ▼
[2. Producer Loop (Main Thread)]
    Enqueues client_fd, client_ip, client_port into Request Queue
    Protected by: pthread_mutex_t queue.mutex
    Signals: pthread_cond_signal(&cond_not_empty)
        │
        ▼
[3. Stage-1 Request Queue (Bounded Ring Buffer, Capacity 32)]
        │
        ├── Worker Thread 0 (pthread consumer)
        ├── Worker Thread 1 (pthread consumer)
        ├── Worker Thread 2 (pthread consumer)
        └── Worker Thread 3 (pthread consumer)
            │
            │ Dequeues request, releases queue.mutex, signals cond_not_full
            ▼
[4. Worker Request Processing & Routing (http.c)]
    - Reads raw HTTP request from client_fd: read(client_fd, buffer)
    - Parses method, path, HTTP headers, query parameters, JSON payload
    - Routes to Auth, Student, Faculty, CRUD, Status, Logs, or Static File
        │
        │ If Academic Data or CRUD Operation:
        ▼
[5. Multi-Level Data Synchronization & Admission Control]
    - Level 1: sem_wait(&g_data_access_sem) [Limits to 2 Concurrent Slots]
    - Level 2: pthread_mutex_lock(&g_data_mutex) [Fine-Grained Mutex]
    - Level 3: Direct Memory Access via mmap() pointer (university.dat)
    - Level 4: Flush to Disk: msync(g_mmap_store, size, MS_ASYNC / MS_SYNC)
    - Level 5: pthread_mutex_unlock(&g_data_mutex)
    - Level 6: sem_post(&g_data_access_sem)
        │
        │ Formats response body & constructs HttpResponseItem
        ▼
[6. Stage-2 Response Queue (Circular Ring Buffer, Capacity 32)]
    Enqueues response item: {client_fd, status, headers, body / file_path}
    Protected by: pthread_mutex_t resp_queue.mutex
    Signals: pthread_cond_signal(&resp_queue.cond_not_empty)
        │
        ▼
[7. Dedicated Response Dispatcher Thread (response_queue.c)]
    - Consumes from Response Queue
    - Formats RFC-compliant HTTP status lines & response headers
    - Single-point socket writer: write(client_fd, buffer) / file streaming
    - Closes client_fd (guaranteeing exact single-point descriptor cleanup)
    - Logs transaction metrics to transactions.log
        │
        ▼
Browser Client Receives JSON / HTML / Static Asset Response
```

---

## 3. Core OS Architectural Subsystems

### A. Two-Stage Queue Pipeline & Thread Pool
- **Why Two Queues?** In traditional naive thread pools, worker threads process business logic AND perform blocking network socket writes back to clients. Under slow client connections or slow network conditions, worker threads remain blocked waiting on network sockets. The University ERP architecture cleanly decouples request processing from response transmission:
  - **Stage 1 (Request Queue)**: The main accept thread enqueues incoming socket descriptors; 4 worker threads dequeue requests and execute CPU/data logic.
  - **Stage 2 (Response Queue)**: Worker threads enqueue formatted HTTP response items and return immediately to the pool.
  - **Response Dispatcher Thread**: A dedicated POSIX thread consumes from the response queue, writes HTTP headers and bodies to `client_fd`, and calls `close(client_fd)`.

### B. Memory-Mapped Storage Layer (`mmap()`, `msync()`, `munmap()`)
- **Binary Format**: The database is stored in `server/data/university.dat`, starting with a 32-byte header:
  - `magic`: `0x554E4956` ("UNIV" in ASCII)
  - `version`: `1`
  - `student_count`, `faculty_count`, `course_count`
  - `transaction_count`, `last_sync_time`
  - Fixed-size arrays of `Student`, `Faculty`, `Course`, `Marks`, and `Attendance` structs.
- **Operating System Advantage**: Traditional file I/O requires `lseek()`, `read()`, and `write()`, repeatedly copying data between kernel buffers and user-space heap memory. `mmap()` maps the binary data file directly into the process virtual address space. Memory reads and writes modify pages in RAM, and `msync(..., MS_SYNC)` atomically flushes dirty pages directly to the storage block layer.
- **Persistence Across Restarts**: When the server restarts, `mmap_data_init()` maps `university.dat`, verifies the magic bytes and version, and immediately accesses the existing records without file parsing overhead.

### C. Real-Time Linux Kernel Introspection (`/proc`)
- Telemetry is queried live directly from the virtual `/proc` filesystem:
  - `/proc/self/stat`: Parsed via `sscanf()` to obtain process state (`R`, `S`, `D`), number of threads, user-mode CPU ticks (`utime`), kernel-mode CPU ticks (`stime`), and virtual memory size (`vsize`).
  - `/proc/uptime`: Provides total system uptime for exact CPU percentage calculations.
  - `/proc/self/status`: Parsed line-by-line to extract resident set size (`VmRSS`).
  - `/proc/self/fd/`: Directory entries counted via `opendir()`/`readdir()` to accurately report open file descriptors.

### D. Dynamic Configuration Reload via SIGHUP
- The server reads configuration from `server/config/server.conf`.
- When an administrator sends `SIGHUP` (`kill -HUP <PID>` or via the UI button):
  - The POSIX signal handler sets `volatile sig_atomic_t g_sighup_received = 1`.
  - The main server accept loop checks this flag and calls `config_reload()`.
  - Concurrency slots (`data_access_slots`) and logging configurations are updated live without dropping active TCP connections or restarting the server.

### E. Thread-Safe Structured File Logger
- Two dedicated append-only log streams:
  - `server/logs/server.log`: System lifecycle, worker events, connection lifecycle, warnings, and errors.
  - `server/logs/transactions.log`: Auditable transaction log recording student CRUD operations, marks mutations, and attendance updates with ISO-8601 timestamps, worker IDs, latency, and status.
- Synchronized with `pthread_mutex_t g_logger_mutex` and flushed using `fflush()`.

---

## 4. Concurrency & Synchronization Primitives Summary

| Primitive | Mechanism | Purpose |
| :--- | :--- | :--- |
| **Worker Threads** | `pthread_t` (Fixed Pool of 4) | Parallel request processing |
| **Request Queue** | `pthread_mutex_t` + 2 `pthread_cond_t` | Bounded circular FIFO queue (capacity 32) |
| **Response Queue** | `pthread_mutex_t` + 2 `pthread_cond_t` | Decoupled response delivery pipeline (capacity 32) |
| **Dispatcher Thread** | `pthread_t` (Dedicated) | Single-point socket streaming & descriptor cleanup |
| **Admission Control**| `sem_t` (`g_data_access_sem`) | Limits concurrent DB transactions to 2 slots |
| **Academic Store** | `pthread_mutex_t` (`g_data_mutex`) | Mutual exclusion over mmap records |
| **Shared Telemetry**| `pthread_mutex_t` (`g_stats_mutex`)| Thread-safe counters for requests and workers |
| **File Logging** | `pthread_mutex_t` (`g_logger_mutex`) | Serialized atomic disk writes |

---

## 5. Verification & Testing Hierarchy

The system includes automated bash test suites verified through a unified `Makefile`:
- `make test`: Phase 1 HTTP socket communication (7 tests).
- `make test-phase2`: Phase 2 role-based authentication and academic dashboard (8 tests).
- `make test-concurrency`: Phase 3 high-concurrency burst and mutex protection (8 tests).
- `make test-phase4`: Phase 4 mmap persistence, `/proc` monitor, and SIGHUP reload (7 tests).
- `make test-phase5`: Phase 5 OS telemetry, full CRUD, and 50-request stress test (6 tests).
- `make test-all`: Master runner verifying all 36 tests across all 5 phases with 100% pass rate.
