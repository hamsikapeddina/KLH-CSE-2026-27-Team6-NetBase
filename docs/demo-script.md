# University ERP — 5–10 Minute Live Demonstration Script

Follow this sequential, step-by-step demonstration script during your presentation or evaluation.

---

## Demonstration Overview

| Step | Action | Key Talking Point | Time |
| :--- | :--- | :--- | :--- |
| **Step 1** | Start C Server in Terminal | Thread pool, queues, mmap, and logging initialization | 1 min |
| **Step 2** | Open Web Browser (`localhost:3000`) | Clean Vanilla JS frontend communicating over TCP | 30 sec |
| **Step 3** | Student Portal Login | Academic profile, real-time GPA, attendance, grade chart | 1 min |
| **Step 4** | Faculty Portal & Grade Mutation | Live marks/attendance updates and mutex synchronization | 1 min |
| **Step 5** | Tour the OS Monitor | Live Linux `/proc` metrics, memory, FDs, worker states | 1.5 min |
| **Step 6** | High-Concurrency Stress Burst | 32 concurrent requests distributed across 4 POSIX workers | 1 min |
| **Step 7** | Explain Synchronization Primitives | Mutexes, condition variables, and 2-slot semaphore | 1 min |
| **Step 8** | Persistence Across Restart | Modify record, restart server, verify data persists via `mmap` | 1 min |
| **Step 9** | Live SIGHUP Config Reload | Dynamic reload without restarting or dropping clients | 45 sec |
| **Step 10** | Live Log Streamer Inspection | Structured `server.log` and `transactions.log` | 45 sec |
| **Step 11** | Graceful Server Shutdown | `kill -TERM <PID>`, thread pool joining, clean `munmap` | 45 sec |

---

## Step-by-Step Instructions & Script

### Step 1 — Start the Server
1. Open your Linux terminal:
   ```bash
   make
   make run
   # Or directly: ./bin/server 3000
   ```
2. **Explain to Evaluator**:
   > *"This starts my Linux C server. The server creates the worker threads and initializes the queues, semaphore, mmap storage and logging."*
3. Point out terminal log entries showing:
   - 4 worker threads spawned (`Worker 0, 1, 2, 3`).
   - Mmap data loaded from `server/data/university.dat`.
   - POSIX counting semaphore initialized to 2 slots.
   - Dedicated response dispatcher thread online.

---

### Step 2 — Open the Browser
1. Navigate to:
   ```text
   http://localhost:3000
   ```
2. **Explain to Evaluator**:
   > *"The frontend is built purely with vanilla HTML5, CSS, and modern JavaScript without heavy client frameworks. It communicates directly with our C backend over standard HTTP/1.1 REST endpoints."*

---

### Step 3 — Student Login & Dashboard
1. Click the **Student Quick-Login** button (`aarav.sharma@univ.edu` / `pass101`).
2. Show:
   - Student information: Aarav Sharma (ID: `S101`), Computer Science, Year 3.
   - Semester GPA (3.92) and Overall Attendance (89.5%).
   - Course grade cards and visual breakdown chart (Operating Systems, Networks, DBMS).
3. **Explain to Evaluator**:
   > *"The student portal queries our memory-mapped storage to compute current averages and render the transcript live."*

---

### Step 4 — Faculty Portal & Academic Mutation
1. Click **Switch Role / Quick Fill** -> Select **Faculty: Dr. Vikram Rao** (`vikram.rao@univ.edu` / `fac201`).
2. Show the Faculty Gradebook for **CS301: Operating Systems**.
3. Click **✏️ Edit Marks** for student Aarav Sharma.
4. Modify Midterm marks from `42` to `48` and submit.
5. **Explain to Evaluator**:
   > *"When marks are submitted, the worker thread acquires a database semaphore slot, locks the data mutex, writes directly into the memory-mapped file, and calls msync() to flush dirty pages to disk."*

---

### Step 5 — OS Monitor Inspection
1. Click the **OS Monitor** tab in the top navigation bar.
2. Highlight the real-time Linux kernel telemetry metrics:
   - **Server PID**: Actual Linux PID (`getpid()`).
   - **Resident Set Size (VmRSS)**: Physical RAM usage extracted live from `/proc/self/stat`.
   - **Virtual Memory Size (VSize)**: Virtual address space size.
   - **Open File Descriptors**: Count of active descriptors in `/proc/self/fd/`.
   - **CPU Time & Ticks**: User and system ticks from the kernel.
   - **Thread Pool Grid**: Real-time status of all 4 workers (`Worker 0, 1, 2, 3`).
   - **Queue Depths**: Request Queue and Response Queue.
   - **Counting Semaphore**: 2 Concurrent Data Access Slots.
   - **Memory Map Status**: File path, mapped bytes, magic bytes (`0x554E4956`), and sync timestamp.

---

### Step 6 — Concurrency Stress Test
1. In the OS Monitor, scroll to the **Concurrency & Stress Testing** section.
2. Select **32 Concurrent Requests** and click **🚀 Fire Concurrent Burst**.
3. **Explain to Evaluator**:
   > *"These requests enter the shared request queue and are distributed among the four worker threads."*
4. Point out:
   - The worker distribution output showing all 4 workers actively sharing the load.
   - Total elapsed time (typically under 10–30ms for 32 requests).
   - Zero dropped or corrupted requests.

---

### Step 7 — Synchronization Primitives Explanation
1. Point to the Synchronization Architecture diagram in the OS Monitor.
2. **Explain to Evaluator**:
   > *"The mutex protects shared structures, the condition variable allows workers to sleep while the queue is empty, and the semaphore limits academic data access to two concurrent workers."*

---

### Step 8 — Persistence Across Server Restart
1. Go to the **CRUD Management** sub-tab in the OS Monitor.
2. Create a new student: ID `S199`, Name `Karan Patel`, Dept `Computer Science`, Year `1`, Sec `A`.
3. In the terminal, stop the server (`Ctrl+C` or `kill -TERM <PID>`).
4. Restart the server:
   ```bash
   make run
   ```
5. Reload the browser page and view the student list.
6. **Explain to Evaluator**:
   > *"Student S199 is immediately present. Because data is stored in the persistent binary file university.dat via POSIX mmap() and committed with msync(), no records are lost across server reboots."*

---

### Step 9 — Live SIGHUP Configuration Reload
1. Click the **⚡ Trigger SIGHUP (Live Reload)** button in the OS Monitor header.
   *(Or run `kill -HUP <PID>` in terminal).*
2. Point out:
   - The reload counter increments in the header (`Reloads: 1`, `Last Reload: [Timestamp]`).
   - The server did NOT restart; ongoing client sessions were not interrupted.
3. **Explain to Evaluator**:
   > *"SIGHUP tells the running server to reload supported configuration without restarting. The signal handler sets an atomic flag, and the main thread safely rereads server.conf in normal execution context."*

---

### Step 10 — Thread-Safe Logs
1. Click the **Log Streamer** tab in the OS Monitor.
2. Toggle between **Server Log (`server.log`)** and **Transaction Log (`transactions.log`)**.
3. Point out:
   - Every transaction records the worker thread ID, operation, record ID, latency in milliseconds, and status.
   - Mutex synchronization guarantees that log lines from different threads never overlap or corrupt.

---

### Step 11 — Graceful Server Shutdown
1. In a separate terminal terminal window, trigger SIGTERM:
   ```bash
   kill -TERM $(pgrep -f "bin/server")
   ```
2. Observe the shutdown sequence in the server terminal:
   - Listening socket closed.
   - Workers notified via `pthread_cond_broadcast`.
   - All 4 workers joined cleanly via `pthread_join`.
   - Response queue drained and dispatcher thread joined.
   - `msync(MS_SYNC)` executed and `munmap()` called.
   - Log files closed.
3. **Explain to Evaluator**:
   > *"The server stops accepting new clients, allows queued work and responses to finish, joins the worker and dispatcher threads, synchronizes and unmaps the data file, and closes resources."*
