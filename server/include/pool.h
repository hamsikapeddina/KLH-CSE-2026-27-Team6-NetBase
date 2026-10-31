#ifndef POOL_H
#define POOL_H

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define NUM_WORKERS 4
#define QUEUE_CAPACITY 32
#define DATA_ACCESS_SLOTS 2  /* Semaphore capacity: max concurrent data store access slots */

/* Request Queue Item: Holds connection metadata produced by accept() */
typedef struct {
    int client_fd;
    char client_ip[64];
    int client_port;
    time_t enqueue_time;
} QueueItem;

/* Thread-safe Bounded Circular Request Queue */
typedef struct {
    QueueItem items[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
} RequestQueue;

/* Worker Thread Metadata */
typedef struct {
    int id;
    pthread_t thread;
    unsigned long tasks_completed;
    int is_busy;
    char current_path[512];
} WorkerInfo;

/* Thread Pool Container */
typedef struct {
    WorkerInfo workers[NUM_WORKERS];
    RequestQueue queue;
    int num_workers;
    int is_running;
} ThreadPool;

extern ThreadPool g_pool;
extern sem_t g_data_access_sem;
extern pthread_mutex_t g_stats_mutex;
extern pthread_mutex_t g_data_mutex;

/* Lifecycle and Queue Functions */
int thread_pool_init(int num_workers);
int thread_pool_enqueue(int client_fd, const char *client_ip, int client_port);
void thread_pool_shutdown(void);

/* Semaphore helpers: Resource-counting control for limited database slots */
void data_access_enter(int worker_id, const char *operation);
void data_access_exit(int worker_id, const char *operation);

/* Diagnostic helper queries */
int thread_pool_get_queue_size(void);
int thread_pool_get_busy_workers(void);
int thread_pool_get_idle_workers(void);

#endif /* POOL_H */
