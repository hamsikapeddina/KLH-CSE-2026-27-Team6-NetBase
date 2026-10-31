# Operating Systems Concepts — Technical Reference & Viva Guide

This document provides concise, viva-oriented explanations for every Operating Systems concept implemented in the University ERP native C server.

---

## 1. POSIX Sockets (`socket`, `bind`, `listen`, `accept`)
- **What they are**: A standard POSIX API for network inter-process communication (IPC) over the transport layer (TCP/IP).
- **Why used**: Sockets allow the C server process to listen on a designated network port (`3000`), accept incoming TCP client connections from web browsers or HTTP clients, and transmit bi-directional data streams over IPv4 (`AF_INET`, `SOCK_STREAM`).
- **Code Reference**: `server/src/main.c` (`socket()`, `setsockopt()`, `bind()`, `listen()`, and `accept()`).

---

## 2. File Descriptors (`int client_fd`, `int server_fd`)
- **What they represent**: In Linux ("everything is a file"), a file descriptor is an integer index in the process's per-process kernel file descriptor table that points to an open file, socket, pipe, or device.
- **How client/server sockets use them**:
  - `g_server_fd`: The passive listening socket descriptor that awaits incoming connections.
  - `client_fd`: Created by `accept()`, representing an active, dedicated TCP connection with a specific browser client. The server reads HTTP requests from it and writes HTTP responses to it.
- **FD Lifecycle**: Handed from `accept()` in `main.c` -> enqueued into request queue -> dequeued by worker -> transferred to response queue -> written and finally closed (`close(fd)`) by the response dispatcher thread.

---

## 3. POSIX Threads (`pthreads`)
- **Why worker threads are used**: Threads allow a single process to execute multiple tasks concurrently, sharing the same virtual memory space (code, heap, global variables) while maintaining independent CPU execution stacks and program counters.
- **Advantage over multiple processes**: Threads have dramatically lower creation and context-switching overhead compared to `fork()`-ed processes because memory address spaces and page tables do not need to be duplicated.
- **Code Reference**: `server/src/pool.c` (`pthread_create`, `pthread_join`, `pthread_self`).

---

## 4. Thread Pool Pattern
- **Why four workers are created instead of one thread per request**:
  1. **Eliminates thread creation/destruction overhead**: Creating a new thread per request causes high kernel overhead allocating thread stacks (typically 8MB virtual memory per thread) and destroying them.
  2. **Bounds resource consumption**: Under a high load (e.g., 100 simultaneous requests), creating 100 threads would overwhelm CPU cores with excessive context-switch thrashing and memory exhaustion. A fixed pool of 4 workers caps concurrency to match available CPU cores, providing predictable throughput and stability.
- **Code Reference**: `server/src/pool.c` (`ThreadPool g_pool`, `NUM_WORKERS = 4`).

---

## 5. Bounded Request Queue
- **Why incoming requests are buffered**: A circular FIFO buffer (capacity 32) sits between the main accept thread (producer) and worker threads (consumers).
- **Benefit**: Decouples network connection acceptance from request execution. The main thread can immediately accept an incoming connection and enqueue it in microseconds without waiting for a worker to finish an active database operation or calculation.
- **Code Reference**: `server/src/pool.c` (`g_pool.queue.items[32]`, `head`, `tail`, `count`).

---

## 6. Bounded Response Queue & Dedicated Dispatcher
- **Why completed responses are buffered separately**:
  1. **Decouples processing from network transmission**: If a client has a slow network connection (slowloris effect), writing the HTTP response to the client socket could block for hundreds of milliseconds.
  2. **Prevents worker starvation**: By pushing completed responses into the response queue, workers instantly return to handling new requests.
  3. **Single-point socket lifecycle**: A dedicated dispatcher thread (`response_dispatcher_thread`) consumes responses, writes data to sockets, and performs the single `close(client_fd)`, eliminating race conditions and descriptor leaks.
- **Code Reference**: `server/src/response_queue.c` (`g_response_queue`, `response_dispatcher_thread`).

---

## 7. Mutual Exclusion (POSIX Mutexes — `pthread_mutex_t`)
- **What race condition it prevents**: When two or more threads concurrently read, modify, and write shared global variables without synchronization, the interleaving of machine instructions leads to data inconsistency, missed updates, or corrupted pointers.
- **Where mutexes are used**:
  - `g_pool.queue.mutex`: Prevents two workers or producer from corrupting request queue indices (`head`, `tail`, `count`).
  - `g_stats_mutex`: Prevents lost updates to shared counters (`total_requests`, `completed_requests`).
  - `g_data_mutex`: Prevents simultaneous writes to academic student/marks/attendance records in the memory map.
  - `g_response_queue.mutex`: Protects response queue ring buffer pointers.
  - `g_logger_mutex`: Serializes file writing so log lines from different threads do not interweave into gibberish.
- **Code Reference**: `server/src/pool.c`, `server/src/data.c`, `server/src/response_queue.c`, `server/src/logger.c`.

---

## 8. Condition Variables (`pthread_cond_t`)
- **Why workers sleep instead of busy-waiting**:
  - Without condition variables, worker threads would have to run continuous `while(1)` loops checking `if (count > 0)` (busy waiting/polling), which pins CPU usage at 100% and starves other threads.
  - Condition variables allow worker threads to put themselves into a sleep state (`pthread_cond_wait`), relinquishing the CPU. The operating system kernel awakens them immediately when the producer signals (`pthread_cond_signal`) that a new request has arrived.
- **Spurious Wakeups**: POSIX permits threads to wake up without a signal; wrapping `pthread_cond_wait()` in a `while` loop re-checks the condition predicate and guarantees safety.
- **Code Reference**: `server/src/pool.c` (`cond_not_empty`, `cond_not_full`).

---

## 9. Counting Semaphore (`sem_t`)
- **Why the count is 2**:
  - While the server has 4 worker threads running concurrently, the counting semaphore (`g_data_access_sem`) restricts concurrent access to the academic data store to at most 2 simultaneous worker threads (`DATA_ACCESS_SLOTS = 2`).
  - This models realistic database connection pool throttling or hardware I/O resource admission control, preventing I/O bus saturation.
- **Difference from Mutex**: A mutex is binary (0 or 1) and must be unlocked by the thread that locked it (ownership). A counting semaphore maintains an integer count and can be used for signaling and capacity management across threads.
- **Code Reference**: `server/src/pool.c` (`sem_init(&g_data_access_sem, 0, 2)`, `sem_wait`, `sem_post`).

---

## 10. Memory-Mapped Files (`mmap()`)
- **How the data file becomes accessible through memory mapping**:
  - `mmap()` asks the Linux kernel to map the physical disk file `university.dat` directly into the process's virtual memory address space.
  - The function returns a regular C memory pointer (`MmapUniversityStore* g_mmap_store`).
  - Reading and writing academic records is performed directly as ordinary C pointer/struct operations (`g_mmap_store->students[i] = ...`), completely bypassing traditional `lseek()`, `read()`, and `write()` system call overhead.
- **Code Reference**: `server/src/mmap_data.c` (`mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`).

---

## 11. Memory Synchronization (`msync()`)
- **How modified mapped pages are synchronized with the backing file**:
  - When memory pages mapped with `MAP_SHARED` are modified in RAM, they become "dirty pages" in the Linux page cache.
  - The Linux kernel eventually flushes dirty pages lazily, but to guarantee persistence against crashes or power failures, the server calls `msync(g_mmap_store, size, MS_ASYNC / MS_SYNC)`.
  - `MS_SYNC` forces an immediate synchronous write to physical storage before continuing.
- **Code Reference**: `server/src/mmap_data.c` (`mmap_data_sync()`).

---

## 12. Linux Signals (`SIGHUP`, `SIGINT`, `SIGTERM`)
- **What they are**: Asynchronous software interrupts sent by the Linux kernel to notify a process of an event.
- **SIGHUP (Signal 1 - Hangup)**:
  - Traditionally sent when a controlling terminal closes; in POSIX servers, it is used by convention to trigger dynamic configuration reloads.
  - When caught, the signal handler sets an atomic flag (`g_sighup_received = 1`). The main thread detects this flag and calls `config_reload()` to reread `server.conf` without restarting the process or dropping active clients.
- **SIGINT (Signal 2 - Interrupt) & SIGTERM (Signal 15 - Terminate)**:
  - Used for graceful server shutdown.
  - Initiates `server_stop()`: stops accepting new sockets, wakes and joins all 4 worker threads, drains the response queue, joins the dispatcher thread, synchronizes and unmaps `mmap` memory, closes log files, and exits cleanly.
- **Code Reference**: `server/src/main.c` (`sigaction()`, `sighup_handler`, `sigint_handler`).

---

## 13. Virtual Process Filesystem (`/proc`)
- **How Linux process statistics are obtained**:
  - `/proc` is a pseudo-filesystem generated dynamically in memory by the Linux kernel; it contains no real disk files.
  - The server reads kernel structures directly:
    - `/proc/self/stat`: Parsed via `sscanf()` to extract process state (`R`, `S`), number of threads, user CPU ticks (`utime`), kernel CPU ticks (`stime`), and virtual memory size (`vsize`).
    - `/proc/self/status`: Read to extract exact Resident Set Size (`VmRSS`), the physical RAM currently consumed.
    - `/proc/self/fd/`: Read with `opendir()` and `readdir()` to count active open file descriptors.
    - `/proc/uptime`: Read to obtain total OS uptime for CPU percentage calculations.
- **Code Reference**: `server/src/proc_monitor.c` (`proc_get_metrics()`).

---

## 14. Thread-Safe File Logging
- **Why concurrent log writes require synchronization**:
  - Standard C `printf()` and `fprintf()` are not atomic when called simultaneously from multiple worker threads writing to the same file descriptor. Without locking, characters or lines from different threads would become interleaved and corrupted.
  - `g_logger_mutex` ensures that each log entry (timestamp, thread ID, level, message) is formatted and flushed to disk (`fflush()`) atomically as an indivisible critical section.
- **Code Reference**: `server/src/logger.c` (`logger_server()`, `logger_transaction()`).
