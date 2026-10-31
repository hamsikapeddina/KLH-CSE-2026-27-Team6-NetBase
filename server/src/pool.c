#include "pool.h"
#include "server.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ThreadPool g_pool = {0};
sem_t g_data_access_sem;
pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Worker thread consumer routine */
static void *worker_thread_func(void *arg) {
    WorkerInfo *worker = (WorkerInfo *)arg;
    server_log("INFO", "[Worker %d] Thread online (Thread ID: %lu). Ready for request queue.",
               worker->id, (unsigned long)pthread_self());

    while (1) {
        QueueItem item;

        /* Step 1: Acquire queue mutex to access shared ring buffer */
        pthread_mutex_lock(&g_pool.queue.mutex);

        /* Step 2: Sleep on condition variable while queue is empty */
        /* Worker sleeps here until a request enters the shared queue. */
        while (g_pool.queue.count == 0 && !g_pool.queue.shutdown) {
            pthread_cond_wait(&g_pool.queue.cond_not_empty, &g_pool.queue.mutex);
        }

        /* If shutdown was signaled and no pending requests remain in queue, exit */
        if (g_pool.queue.shutdown && g_pool.queue.count == 0) {
            pthread_mutex_unlock(&g_pool.queue.mutex);
            break;
        }

        /* Step 3: Remove request from circular ring buffer */
        item = g_pool.queue.items[g_pool.queue.head];
        g_pool.queue.head = (g_pool.queue.head + 1) % QUEUE_CAPACITY;
        g_pool.queue.count--;

        /* Step 4: Signal any producer thread waiting on cond_not_full */
        pthread_cond_signal(&g_pool.queue.cond_not_full);

        /* Step 5: Release queue mutex BEFORE processing the request */
        pthread_mutex_unlock(&g_pool.queue.mutex);

        /* Update shared statistics (Thread-safe via stats_mutex) */
        pthread_mutex_lock(&g_stats_mutex);
        worker->is_busy = 1;
        g_stats.busy_workers++;
        if (g_stats.idle_workers > 0) g_stats.idle_workers--;
        g_stats.pending_requests = g_pool.queue.count;
        g_stats.current_queue_size = g_pool.queue.count;
        pthread_mutex_unlock(&g_stats_mutex);

        server_log("DEBUG", "[Worker %d] Dequeued connection FD=%d from %s:%d (Queue remaining: %d)",
                   worker->id, item.client_fd, item.client_ip, item.client_port, g_pool.queue.count);

        /* Step 6: Process client request (read, parse, handle, respond) */
        process_client_request(worker->id, item.client_fd, item.client_ip, item.client_port);

        /* Note: client_fd is now handed off to the response queue & response dispatcher thread,
         * which writes the response buffer to the socket and performs the final close(client_fd). */

        /* Step 7: Return worker to idle state and update statistics */
        pthread_mutex_lock(&g_stats_mutex);
        worker->is_busy = 0;
        worker->tasks_completed++;
        worker->current_path[0] = '\0';
        if (g_stats.busy_workers > 0) g_stats.busy_workers--;
        g_stats.idle_workers++;
        g_stats.completed_requests++;
        if (g_stats.active_clients > 0) g_stats.active_clients--;
        g_stats.pending_requests = g_pool.queue.count;
        g_stats.current_queue_size = g_pool.queue.count;
        pthread_mutex_unlock(&g_stats_mutex);

        server_log("DEBUG", "[Worker %d] Finished request. Completed tasks by worker: %lu",
                   worker->id, worker->tasks_completed);
    }

    server_log("INFO", "[Worker %d] Worker thread shutting down cleanly.", worker->id);
    return NULL;
}

int thread_pool_init(int num_workers) {
    if (num_workers <= 0 || num_workers > NUM_WORKERS) {
        num_workers = NUM_WORKERS;
    }

    memset(&g_pool, 0, sizeof(g_pool));
    g_pool.num_workers = num_workers;
    g_pool.is_running = 1;

    /* Initialize request queue synchronization primitives */
    g_pool.queue.head = 0;
    g_pool.queue.tail = 0;
    g_pool.queue.count = 0;
    g_pool.queue.shutdown = 0;

    pthread_mutex_init(&g_pool.queue.mutex, NULL);
    pthread_cond_init(&g_pool.queue.cond_not_empty, NULL);
    pthread_cond_init(&g_pool.queue.cond_not_full, NULL);

    pthread_mutex_init(&g_stats_mutex, NULL);
    pthread_mutex_init(&g_data_mutex, NULL);

    /* Initialize POSIX Counting Semaphore for limiting database transaction slots */
    /* Semaphore limits simultaneous academic-data access to 2 workers. */
    if (sem_init(&g_data_access_sem, 0, DATA_ACCESS_SLOTS) != 0) {
        perror("sem_init failed");
        server_log("ERROR", "Failed to initialize POSIX semaphore");
        return -1;
    }

    server_log("INFO", "[SEMAPHORE] Initialized g_data_access_sem with %d concurrent slots", DATA_ACCESS_SLOTS);

    /* Update initial stats */
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.worker_count = num_workers;
    g_stats.busy_workers = 0;
    g_stats.idle_workers = num_workers;
    g_stats.queue_capacity = QUEUE_CAPACITY;
    g_stats.current_queue_size = 0;
    g_stats.pending_requests = 0;
    g_stats.completed_requests = 0;
    g_stats.failed_requests = 0;
    pthread_mutex_unlock(&g_stats_mutex);

    /* Spawn fixed worker thread pool */
    for (int i = 0; i < num_workers; i++) {
        g_pool.workers[i].id = i;
        g_pool.workers[i].tasks_completed = 0;
        g_pool.workers[i].is_busy = 0;
        g_pool.workers[i].current_path[0] = '\0';

        if (pthread_create(&g_pool.workers[i].thread, NULL, worker_thread_func, &g_pool.workers[i]) != 0) {
            perror("pthread_create failed");
            server_log("ERROR", "Failed to create worker thread %d", i);
            return -1;
        }
    }

    server_log("INFO", "[THREAD POOL] Spawned %d worker threads successfully.", num_workers);
    return 0;
}

int thread_pool_enqueue(int client_fd, const char *client_ip, int client_port) {
    /* Step 1: Acquire queue lock */
    pthread_mutex_lock(&g_pool.queue.mutex);

    if (g_pool.queue.shutdown) {
        pthread_mutex_unlock(&g_pool.queue.mutex);
        return -1;
    }

    /* Step 2: If queue is bounded and full, wait on cond_not_full */
    while (g_pool.queue.count == QUEUE_CAPACITY && !g_pool.queue.shutdown) {
        server_log("WARN", "[QUEUE] Request queue full (%d/%d). Producer waiting...",
                   g_pool.queue.count, QUEUE_CAPACITY);
        pthread_cond_wait(&g_pool.queue.cond_not_full, &g_pool.queue.mutex);
    }

    if (g_pool.queue.shutdown) {
        pthread_mutex_unlock(&g_pool.queue.mutex);
        return -1;
    }

    /* Step 3: Insert item into circular ring buffer */
    QueueItem *item = &g_pool.queue.items[g_pool.queue.tail];
    item->client_fd = client_fd;
    snprintf(item->client_ip, sizeof(item->client_ip), "%s", client_ip ? client_ip : "unknown");
    item->client_port = client_port;
    item->enqueue_time = time(NULL);

    g_pool.queue.tail = (g_pool.queue.tail + 1) % QUEUE_CAPACITY;
    g_pool.queue.count++;

    /* Step 4: Signal worker thread that a request is ready */
    pthread_cond_signal(&g_pool.queue.cond_not_empty);

    /* Step 5: Release queue mutex */
    pthread_mutex_unlock(&g_pool.queue.mutex);

    /* Update stats */
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.pending_requests = g_pool.queue.count;
    g_stats.current_queue_size = g_pool.queue.count;
    pthread_mutex_unlock(&g_stats_mutex);

    return 0;
}

void thread_pool_shutdown(void) {
    if (!g_pool.is_running) return;

    server_log("INFO", "[THREAD POOL] Initiating graceful shutdown of %d worker threads...", g_pool.num_workers);

    /* Lock queue to set shutdown flag */
    pthread_mutex_lock(&g_pool.queue.mutex);
    g_pool.queue.shutdown = 1;
    g_pool.is_running = 0;

    /* Wake all sleeping worker threads waiting on condition variable */
    pthread_cond_broadcast(&g_pool.queue.cond_not_empty);
    pthread_cond_broadcast(&g_pool.queue.cond_not_full);
    pthread_mutex_unlock(&g_pool.queue.mutex);

    /* Wait for each worker thread to finish active task and terminate */
    for (int i = 0; i < g_pool.num_workers; i++) {
        pthread_join(g_pool.workers[i].thread, NULL);
        server_log("INFO", "[Worker %d] Joined successfully. (Tasks handled: %lu)",
                   i, g_pool.workers[i].tasks_completed);
    }

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&g_pool.queue.mutex);
    pthread_cond_destroy(&g_pool.queue.cond_not_empty);
    pthread_cond_destroy(&g_pool.queue.cond_not_full);
    pthread_mutex_destroy(&g_stats_mutex);
    pthread_mutex_destroy(&g_data_mutex);

    /* Destroy POSIX Semaphore */
    sem_destroy(&g_data_access_sem);

    server_log("INFO", "[THREAD POOL] All workers joined and synchronization primitives destroyed cleanly.");
}

/* Semaphore Data-Access Controls */
void data_access_enter(int worker_id, const char *operation) {
    server_log("DEBUG", "[Worker %d] [SEMAPHORE] Requesting DB access slot for '%s' (sem_wait)...",
               worker_id, operation ? operation : "unknown");
    sem_wait(&g_data_access_sem);
    server_log("DEBUG", "[Worker %d] [SEMAPHORE] Acquired DB access slot for '%s'",
               worker_id, operation ? operation : "unknown");
}

void data_access_exit(int worker_id, const char *operation) {
    server_log("DEBUG", "[Worker %d] [SEMAPHORE] Releasing DB access slot for '%s' (sem_post)",
               worker_id, operation ? operation : "unknown");
    sem_post(&g_data_access_sem);
}

int thread_pool_get_queue_size(void) {
    int size = 0;
    pthread_mutex_lock(&g_pool.queue.mutex);
    size = g_pool.queue.count;
    pthread_mutex_unlock(&g_pool.queue.mutex);
    return size;
}

int thread_pool_get_busy_workers(void) {
    int busy = 0;
    pthread_mutex_lock(&g_stats_mutex);
    busy = g_stats.busy_workers;
    pthread_mutex_unlock(&g_stats_mutex);
    return busy;
}

int thread_pool_get_idle_workers(void) {
    int idle = 0;
    pthread_mutex_lock(&g_stats_mutex);
    idle = g_stats.idle_workers;
    pthread_mutex_unlock(&g_stats_mutex);
    return idle;
}
