# Concurrent University ERP — Linux/POSIX Client-Server System

> **Operating Systems & Linux System Programming Project (Phases 1–6 Complete)**  
> A high-performance, concurrent academic ERP server written in pure C on Linux using low-level POSIX APIs, featuring a responsive web client and a real-time Operating Systems telemetry monitor.

---

## 📌 System Overview

A Linux-based concurrent University ERP implemented using a C client-server architecture. Multiple student and faculty clients can access academic data concurrently. POSIX threads, mutexes, condition variables, semaphores, memory-mapped files, Linux signals, `/proc`, POSIX sockets, and logging provide safe concurrent access and server monitoring.

The data layer is **a simple persistent file-backed academic data store implemented using POSIX memory mapping (`mmap`)**. Records are laid out directly in a structured binary file (`university.dat`) mapped into process virtual memory, with updates synchronized to disk via `msync()`.

---

## 🛠️ Technologies & Primitives

- **Language**: Standard C (GNU C / C11, compiled with `gcc -Wall -Wextra -O2 -pthread`)
- **Networking**: POSIX Sockets API (`AF_INET`, `SOCK_STREAM`, `socket`, `bind`, `listen`, `accept`, `SO_REUSEADDR`)
- **Concurrency**: POSIX Threads (`pthread_create`, `pthread_join`, `pthread_self`) with a 4-worker thread pool
- **Synchronization**:
  - POSIX Mutexes (`pthread_mutex_t`) for queue, counter, data, and log protection
  - POSIX Condition Variables (`pthread_cond_t`) for producer-consumer sleep/wake coordination
  - POSIX Counting Semaphore (`sem_t`, initialized to 2 slots) for DB admission control
- **Storage**: Memory-Mapped File I/O (`mmap`, `msync`, `munmap`, `ftruncate`, `open`)
- **Kernel Introspection**: Linux Virtual Filesystem (`/proc/self/stat`, `/proc/self/status`, `/proc/self/fd/`, `/proc/uptime`)
- **Signal Handling**: Linux Asynchronous Signals (`sigaction`) for `SIGHUP` (live config reload) and `SIGINT`/`SIGTERM` (graceful shutdown)
- **File I/O & Logging**: Thread-safe synchronized file logging (`server.log` and `transactions.log`)
- **Client Frontend**: Vanilla HTML5, CSS3, ES6 JavaScript (zero external JS/CSS frameworks)

---

## 🏗️ End-to-End System Architecture

```text
Browser Client (Vanilla HTML5 / Modern CSS / ES6 JavaScript)
        │
        │ HTTP Requests (GET, POST over TCP stream)
        ▼
[1. POSIX Socket Layer]
    AF_INET, SOCK_STREAM, SO_REUSEADDR (port 3000 / 8089)
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

## 🚀 How to Build & Run

### 1. Prerequisites
- Linux OS / Ubuntu 20.04+ (or WSL2)
- GCC compiler (`gcc`)
- GNU Make (`make`)
- `curl` (for testing)

### 2. Clean Build
Compile with all warnings enabled and optimization level 2:
```bash
make clean && make
```
*(Produces zero warnings and zero errors; outputs executable to `bin/server`).*

### 3. Run the Server
```bash
# Starts on default port 3000
./bin/server 3000

# Or via Makefile shortcut
make run
```
Access the application in your browser at `http://localhost:3000`.

---

## 🧪 Automated Testing & Verification

Run the master automated test suite verifying all 5 phases:
```bash
make test-all
```

### Actual Test Results
```text
====================================================================
  🎉 ALL PHASES (1, 2, 3, 4, 5) INTEGRATION TESTS PASSED 100%!        
====================================================================
Total Suites Executed : 5
Total Tests Run       : 36
Passed                : 36
Failed                : 0
Success Rate          : 100%
```

Individual test suite targets:
- `make test`: Phase 1 Protocol & Socket Tests (7/7 pass)
- `make test-phase2`: Phase 2 Academic Workflows & Auth (8/8 pass)
- `make test-concurrency`: Phase 3 Multi-Worker Concurrency (8/8 pass)
- `make test-phase4`: Phase 4 mmap, SIGHUP & /proc (7/7 pass)
- `make test-phase5`: Phase 5 Telemetry, CRUD & 50-Req Burst (6/6 pass)

---

## 👥 Demo Accounts

The persistent data store is pre-seeded with realistic student and faculty accounts:

### Student Accounts
| Student ID | Full Name | Email | Password | Department | Year |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **S101** | Aarav Sharma | `aarav.sharma@univ.edu` | `pass101` | Computer Science | Year 3 |
| **S102** | Priya Patel | `priya.patel@univ.edu` | `pass102` | Computer Science | Year 3 |
| **S103** | Rohan Verma | `rohan.verma@univ.edu` | `pass103` | Information Tech | Year 2 |
| **S104** | Ananya Iyer | `ananya.iyer@univ.edu` | `pass104` | Computer Science | Year 3 |

### Faculty Accounts
| Faculty ID | Full Name | Email | Password | Department | Assigned Courses |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **F201** | Dr. Vikram Rao | `vikram.rao@univ.edu` | `fac201` | Computer Science | CS301 (OS), CS304 (Linux) |
| **F202** | Prof. Neha Gupta | `neha.gupta@univ.edu` | `fac202` | Information Tech | CS302 (Networks) |
| **F203** | Dr. Rajesh Kulkarni | `rajesh.k@univ.edu` | `fac203` | Computer Science | CS303 (DBMS) |

*(Quick-login buttons are provided directly on the login screen for rapid demonstration).*

---

## 🖥️ Operating Systems Concepts Demonstrated

1. **POSIX Sockets & File Descriptors**: Network stream communication over IPv4 TCP.
2. **Thread Pool (4 Workers)**: Fixed pre-spawned worker pool eliminating thread creation overhead and thrashing.
3. **Producer-Consumer Queue Pipeline**: Two-stage queue architecture (Request Queue + Response Queue with Dedicated Dispatcher).
4. **Mutual Exclusion (Mutexes)**: Safe concurrent updates to queues, metrics, logs, and academic records.
5. **Condition Variables**: Efficient thread sleeping and waking without busy-wait CPU polling.
6. **Counting Semaphore (2 Slots)**: Dynamic admission control limiting concurrent database mutations.
7. **Memory-Mapped Storage (`mmap()`)**: Zero-copy persistent file backing store mapped directly to virtual memory.
8. **Dirty Page Flushing (`msync()`)**: Guaranteed disk persistence across server restarts.
9. **Linux `/proc` Virtual Filesystem**: Live kernel introspection of RSS memory, virtual size, CPU ticks, and open FDs.
10. **Signal Handling (`SIGHUP`, `SIGINT`, `SIGTERM`)**: Live dynamic configuration reload without restart; clean graceful pipeline drainage.
11. **Thread-Safe Logging**: Serialized dual-stream file logging (`server.log`, `transactions.log`).

---

## 📁 Directory Structure

```text
.
├── Makefile                      # Production build script
├── bin/                          # Output binaries
│   └── server                    # Native Linux C executable
├── server/
│   ├── include/                  # C Header files
│   │   ├── server.h              # Server stats and constants
│   │   ├── http.h                # HTTP protocol parser
│   │   ├── pool.h                # Thread pool and request queue
│   │   ├── response_queue.h      # Response queue and dispatcher
│   │   ├── data.h                # Academic data layer
│   │   ├── mmap_data.h           # Memory-mapped binary store
│   │   ├── logger.h              # Thread-safe logging
│   │   ├── config.h              # Configuration management
│   │   └── proc_monitor.h        # Linux /proc introspection
│   ├── src/                      # C Implementation files
│   │   ├── main.c                # Main accept loop & signal handlers
│   │   ├── http.c                # HTTP parser & routing
│   │   ├── pool.c                # Thread pool & request queue
│   │   ├── response_queue.c      # Response queue & dispatcher thread
│   │   ├── data.c                # Academic ERP data access
│   │   ├── mmap_data.c           # mmap/msync binary persistence
│   │   ├── logger.c              # Synchronized dual file logging
│   │   ├── config.c              # SIGHUP dynamic config reload
│   │   └── proc_monitor.c        # /proc parsing
│   ├── config/
│   │   └── server.conf           # Dynamic configuration file
│   ├── data/
│   │   └── university.dat        # Persistent binary mmap data store
│   └── logs/
│       ├── server.log            # Server lifecycle & error logs
│       └── transactions.log      # Auditable transaction logs
├── web/                          # Frontend assets (served by C server)
│   ├── index.html                # Unified ERP portal & OS Monitor UI
│   ├── css/
│   │   └── style.css             # Responsive styling
│   └── js/
│       └── app.js                # Vanilla JavaScript client
├── docs/                         # Project Documentation
│   ├── architecture.md           # End-to-end architecture & queue pipeline
│   ├── os-concepts.md            # Theory-to-code mapping of all 14 OS concepts
│   ├── requirements-mapping.md   # Faculty assignment requirement traceability
│   ├── demo-script.md            # 5-10 minute presentation demo sequence
│   ├── viva.md                   # 53 viva questions, answers & 1-minute summary
│   └── testing.md                # Comprehensive test matrix
└── tests/                        # Automated Bash test suites
    ├── test_phase1.sh            # Phase 1 tests (7 tests)
    ├── test_phase2.sh            # Phase 2 tests (8 tests)
    ├── test_concurrency.sh       # Phase 3 concurrency tests (8 tests)
    ├── test_phase4.sh            # Phase 4 advanced OS tests (7 tests)
    ├── test_phase5.sh            # Phase 5 telemetry & stress tests (6 tests)
    └── test_all.sh               # Master test runner (36 tests)
```

---

## ⚖️ Limitations & Honest Assessment

- **Data Capacity**: The mmap binary store uses fixed-size struct arrays (`MAX_STUDENTS = 64`, `MAX_COURSES = 32`, etc.). Dynamic expansion requires `mremap()` which is not implemented.
- **Search Complexity**: Record lookup performs linear $O(N)$ searches rather than indexed $O(\log N)$ B-tree lookups.
- **Not a Distributed DBMS**: Designed strictly as a single-host, educational operating-system demonstration, not an enterprise distributed DBMS with Paxos/Raft replication.
- **HTTP Scope**: Implements a dedicated HTTP/1.1 subset; does not support HTTP/2 or HTTP/3 multiplexing.
