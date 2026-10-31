# University ERP — Operating Systems Viva Examination Guide

This guide contains complete, concise answers to all 53 viva examination questions, followed by the memorizable One-Minute Project Summary.

---

## 0. The One-Minute Project Explanation (Memorize This)

> "My project is a Linux-based concurrent University ERP implemented using a C client-server architecture. The frontend is HTML, CSS and JavaScript, while the backend is a POSIX C server using TCP sockets. Multiple clients can connect simultaneously. Incoming requests are placed into a shared bounded request queue and processed by four worker threads. Mutexes protect shared data, condition variables synchronize the producer-consumer queues, and a counting semaphore limits simultaneous academic-data access to two workers. Academic data is stored persistently using a memory-mapped file. Completed responses go through a shared response queue and a dispatcher thread sends them back to clients. Linux signals provide configuration reload and graceful shutdown, `/proc` provides live server resource information, and synchronized logs record server and transaction activity. The OS Monitor makes these operating-system concepts visible during the demonstration."

---

## 1. Project Understanding

### 1. What is the title of your project?
**Concurrent University ERP — Linux/POSIX Client-Server System.**

### 2. What problem does it solve?
It provides a concurrent university portal where students and faculty can access and update academic records (courses, grades, attendance) while demonstrating how an operating system manages concurrency, synchronization, memory, and I/O under load without data corruption.

### 3. Why is it an Operating Systems project?
Instead of relying on high-level web frameworks (like Django, Spring, or Express) or managed databases (like MySQL), the core server is written from scratch in C using low-level POSIX OS primitives: TCP sockets, `pthreads`, mutexes, condition variables, counting semaphores, `mmap()`, `msync()`, `sigaction()`, and kernel `/proc` inspection.

### 4. What is unique about your project?
It exposes all internal OS synchronization and kernel metrics directly in a real-time OS Monitor UI. You can watch worker thread distributions, queue depths, semaphore acquisitions, `/proc` memory/CPU ticks, and trigger dynamic SIGHUP configuration reloads live in the browser.

### 5. What is the frontend?
A lightweight, responsive web interface built using vanilla HTML5, CSS3, and JavaScript (ES6). No frontend frameworks (such as React, Angular, or Vue) are used.

### 6. What is the backend?
A native C program compiled with GCC targeting the Linux/POSIX platform, listening on an IPv4 TCP socket and executing a two-stage queue pipeline with a 4-thread worker pool.

### 7. How do frontend and backend communicate?
Through standard HTTP/1.1 requests (GET, POST) transmitted over streaming TCP sockets (`AF_INET`, `SOCK_STREAM`). Payloads are exchanged using JSON.

### 8. Why did you choose C?
C provides direct access to POSIX system calls, kernel structures, memory mapping, and thread synchronization primitives with zero runtime abstraction, allowing complete control over memory, descriptors, and CPU scheduling.

---

## 2. Concurrency

### 9. Why use threads?
Threads share the same virtual memory space within a process, enabling them to read and write shared data structures without expensive inter-process communication (IPC). Creating and context-switching between threads is much faster than `fork()`-ing processes.

### 10. Why use a thread pool?
Creating a new thread for every client connection incurs heavy CPU and memory overhead (allocating and freeing thread stacks). A thread pool pre-spawns a fixed number of workers at startup and reuses them for all incoming requests.

### 11. Why exactly four workers?
Four workers provide balanced parallel processing for multi-core hardware without causing excessive CPU context-switching or memory thrashing, while matching the educational requirement of demonstrating multi-worker distribution.

### 12. What is the request queue?
A shared bounded circular ring buffer (capacity: 32) that buffers incoming client socket descriptors (`client_fd`) between the main accept thread (producer) and the worker threads (consumers).

### 13. Why is it bounded to 32?
A bounded queue prevents uncontrolled memory growth under load spikes. If the queue were unbounded, an avalanche of requests could consume all available system RAM.

### 14. What is the response queue?
A secondary circular ring buffer (capacity: 32) where worker threads place completed HTTP response objects to be dispatched to clients.

### 15. Why do we need two queues?
To decouple CPU business logic from network socket writing. If a client has a slow network connection, writing to that socket can block. Buffering responses in a separate queue allows worker threads to return immediately to process other requests, while a dedicated dispatcher thread handles network writes.

### 16. What is a mutex?
A mutual exclusion lock (`pthread_mutex_t`). It ensures that only one thread can execute a critical section of code at any given time, preventing race conditions.

### 17. Where are mutexes used?
- `g_pool.queue.mutex`: Protects the request ring buffer (`head`, `tail`, `count`).
- `g_response_queue.mutex`: Protects the response ring buffer.
- `g_stats_mutex`: Protects global counters and metrics.
- `g_data_mutex`: Protects academic records in the memory map.
- `g_logger_mutex`: Serializes atomic writes to log files.

### 18. What is a condition variable?
A synchronization primitive (`pthread_cond_t`) that allows a thread to suspend execution (sleep) until notified that a specific predicate condition is true, avoiding wasteful busy-wait loops.

### 19. What is a spurious wakeup?
A condition where a sleeping thread wakes up from `pthread_cond_wait()` even though no thread signaled or broadcast the condition variable (permitted by POSIX for hardware/kernel efficiency).

### 20. Why use `while` instead of `if` around `pthread_cond_wait()`?
Because of spurious wakeups and race conditions among multiple consumers. The `while` loop re-checks the predicate condition upon waking, ensuring the queue is genuinely non-empty before proceeding.

### 21. What is a semaphore?
A synchronization primitive (`sem_t`) containing an integer counter with atomic wait (`sem_wait` / P) and post (`sem_post` / V) operations.

### 22. Why is the semaphore count 2?
It enforces an admission control policy: even though there are 4 worker threads, at most 2 workers may execute database access operations concurrently, preventing I/O resource saturation.

### 23. Mutex vs semaphore?
- **Mutex**: Binary lock (locked/unlocked) with thread ownership; only the thread that acquired the mutex can release it.
- **Semaphore**: Integer counter without ownership; any thread can increment or decrement it, making it ideal for resource-count limits and producer-consumer signaling.

---

## 3. Memory & Linux

### 24. What is mmap?
A POSIX system call (`mmap()`) that maps a file or device directly into the virtual address space of the calling process.

### 25. Why use mmap?
It allows direct memory access to file data through C pointers without copying buffers back and forth through `read()` and `write()` system calls, utilizing the Linux kernel page cache directly.

### 26. How does data persist after restart?
Because the memory mapping uses `MAP_SHARED`, modifications in RAM are dirty pages backed by the physical file `server/data/university.dat`. When the server restarts, it re-maps the same file and verifies its binary magic header (`0x554E4956`).

### 27. What is `msync()`?
A system call (`msync()`) that forces dirty pages in the memory-mapped region to be written to the underlying disk storage, ensuring durability.

### 28. What is `/proc`?
A pseudo-filesystem in Linux generated on the fly by the kernel that exposes runtime kernel and process information as text streams.

### 29. How do you obtain memory usage?
By reading and parsing `/proc/self/stat` (for `vsize` and resident pages) and `/proc/self/status` (for `VmRSS`, Resident Set Size in KB).

### 30. How do you obtain open file descriptors?
By opening the directory `/proc/self/fd/` using `opendir()` and counting the directory entries with `readdir()`.

---

## 4. Signals & Shutdown

### 31. What is SIGHUP?
Signal 1 (Hangup). In Unix servers, it is used by convention to request dynamic configuration reloads without restarting the running process.

### 32. Why should complex work not happen inside a signal handler?
Signal handlers interrupt normal execution asynchronously. Calling non-reentrant functions (like `malloc()`, `printf()`, or mutex functions) inside a signal handler can cause deadlocks or heap corruption. The handler should only set an atomic flag (`volatile sig_atomic_t`).

### 33. What happens when SIGTERM is received?
The server catches `SIGTERM` or `SIGINT`, invokes `server_stop()`, closes the listening socket, wakes and joins all worker threads, drains the response queue, flushes and unmaps `mmap`, closes log files, and exits cleanly.

### 34. How are worker threads stopped?
The shutdown flag `g_pool.queue.shutdown` is set to 1 under `queue.mutex`, followed by `pthread_cond_broadcast(&cond_not_empty)`. Workers wake up, see the flag, exit their loop, and the main thread calls `pthread_join()` on each thread.

### 35. How is the response queue drained?
The dispatcher thread processes remaining items until the queue count reaches zero and the shutdown flag is set, then exits its loop and is joined via `pthread_join()`.

### 36. When is mmap unmapped?
During `server_stop()` after all worker and dispatcher threads have been joined, ensuring no thread attempts to access the memory pointer after unmapping (`munmap()`).

### 37. Why is `pthread_join()` important?
It blocks until the specified thread terminates, ensuring worker threads finish active tasks, avoiding thread leaks, and guaranteeing that resources are not freed while threads are still using them.

---

## 5. Request Flow

### 38. What happens when a student clicks Login?
The browser sends a `POST /api/auth/login` HTTP request with JSON credentials. The main thread accepts the socket, pushes it to the request queue. A worker dequeues the socket, verifies credentials against `mmap` records, builds a session response, puts it into the response queue, and the dispatcher sends it back.

### 39. What happens when faculty clicks Update Marks?
The browser sends `POST /api/marks/update`. The worker acquires a semaphore slot (`sem_wait`), locks `g_data_mutex`, updates marks in the `mmap` store, recalculates total/grade, calls `msync()`, logs the transaction, unlocks the mutex, releases the semaphore slot (`sem_post`), and enqueues the response.

### 40. What happens when 20 clients send requests simultaneously?
All 20 TCP handshakes are accepted by `main.c` and placed in the request queue. The 4 worker threads continuously dequeue and process them concurrently. The OS Monitor displays how requests were distributed across Worker 0, 1, 2, and 3.

### 41. How does a request reach a worker?
1. `accept()` returns `client_fd`.
2. Main thread locks `queue.mutex`, pushes to `items[tail]`, signals `cond_not_empty`, and unlocks.
3. A sleeping worker wakes from `cond_wait`, removes `items[head]`, signals `cond_not_full`, unlocks, and starts processing.

### 42. How does a response return to the browser?
The worker formats the response and calls `response_queue_enqueue()`. The dedicated response dispatcher thread dequeues it, calls `write()` on `client_fd`, and closes `client_fd`.

### 43. Who owns the client socket at each stage?
1. **Creation**: Main thread (`accept()`).
2. **Transfer**: Request queue buffer.
3. **Processing**: Dequeued worker thread.
4. **Handoff**: Response queue item.
5. **Final Destruction**: Dispatcher thread (`close(client_fd)`).

---

## 6. Safety

### 44. How do you prevent race conditions?
By identifying all shared mutable state (queues, counters, records, logs) and guarding each with dedicated POSIX mutexes and semaphores.

### 45. How do you avoid deadlocks?
By adhering to a strict, global lock acquisition hierarchy:
`queue.mutex` -> `g_data_access_sem` -> `g_data_mutex` -> `g_stats_mutex` -> `g_logger_mutex`.
Locks are never acquired in reverse order, and locks are held for the shortest possible duration.

### 46. How do you prevent double-close?
By enforcing single-point ownership: only the response dispatcher thread calls `close(client_fd)` after sending the response. Workers never close the descriptor.

### 47. How do you prevent lost wakeups?
By always holding the associated mutex when evaluating conditions, calling `pthread_cond_wait()`, and calling `pthread_cond_signal()` or `pthread_cond_broadcast()`.

### 48. What happens if the request queue is full?
The producer blocks on `pthread_cond_wait(&cond_not_full, &queue.mutex)` until a worker dequeues an item, providing natural backpressure against overload.

### 49. What happens if the response queue is full?
The worker thread blocks on `pthread_cond_wait(&cond_not_full, &resp_queue.mutex)` until the dispatcher thread empties a slot.

---

## 7. Limitations & Honest Assessment

### 50. What are the limitations of this project?
- Storage is limited by fixed-size struct arrays in the memory map.
- No indexing (lookups are $O(N)$ linear scans).
- Single-process architecture (does not scale horizontally across multiple machines).
- Simple HTTP subset (not a full HTTP/2 or HTTP/3 web server).

### 51. Is this a production DBMS?
No. It is an educational, file-backed persistent data store implemented via POSIX memory mapping (`mmap`) designed specifically to teach and demonstrate operating systems concepts.

### 52. Why didn't you use MySQL/PostgreSQL?
Using an external database would abstract away all operating system synchronization, locks, concurrency, and file I/O, defeating the core learning objectives of an Operating Systems course.

### 53. What would you improve in a production version?
Add B-tree index structures for $O(\log N)$ record lookups, implement Write-Ahead Logging (WAL) for full ACID transactions, support dynamic resizing of the memory map using `mremap()`, and add TLS/SSL encryption.
