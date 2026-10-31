#include "server.h"
#include "http.h"
#include "data.h"
#include "pool.h"
#include "response_queue.h"
#include "mmap_data.h"
#include "config.h"
#include "logger.h"
#include "proc_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>

ServerStats g_stats = {0};
static int g_server_fd = -1;
static volatile sig_atomic_t g_sighup_received = 0;

void server_stop(void) {
    if (!g_stats.is_running) return;
    g_stats.is_running = 0;

    logger_server("INFO", "[SHUTDOWN] Initiating graceful server shutdown...");

    if (g_server_fd >= 0) {
        int fd = g_server_fd;
        g_server_fd = -1;
        close(fd);
    }

    /* 1. Stop worker threads and drain request queue */
    thread_pool_shutdown();

    /* 2. Stop dispatcher thread and drain response queue */
    response_dispatcher_join();
    response_queue_destroy();

    /* 3. Flush and unmap persistent mmap storage */
    mmap_data_unmap();

    /* 4. Flush and close logs */
    logger_server("INFO", "[SHUTDOWN] All threads joined and mmap flushed. Server stopped cleanly.");
    logger_close();
}

static void sigint_handler(int sig) {
    (void)sig;
    server_stop();
    exit(0);
}

/* Signal handler only sets a flag; reload happens safely in normal context. */
static void sighup_handler(int sig) {
    (void)sig;
    g_sighup_received = 1;
}

int server_init(int port) {
    /* Create directories if they do not exist */
    mkdir("server/logs", 0755);
    mkdir("server/data", 0755);
    mkdir("server/config", 0755);

    /* 1. Initialize configuration from server.conf */
    config_init("server/config/server.conf");
    if (port > 0) g_config.port = port;

    /* 2. Initialize thread-safe logging */
    logger_init(g_config.server_log, g_config.transactions_log);
    logger_server("INFO", "Initializing University ERP POSIX C Server (Phase 4)...");

    g_stats.pid = getpid();
    g_stats.port = g_config.port;
    g_stats.start_time = time(NULL);
    g_stats.total_requests = 0;
    g_stats.completed_requests = 0;
    g_stats.failed_requests = 0;
    g_stats.active_clients = 0;
    g_stats.pending_requests = 0;
    g_stats.worker_count = NUM_WORKERS;
    g_stats.busy_workers = 0;
    g_stats.idle_workers = NUM_WORKERS;
    g_stats.queue_capacity = QUEUE_CAPACITY;
    g_stats.current_queue_size = 0;
    g_stats.is_running = 1;

    /* 3. Initialize memory-mapped persistent storage */
    if (data_init() != 0) {
        logger_server("ERROR", "Failed to initialize persistent mmap academic storage");
        return -1;
    }
    logger_server("INFO", "Mmap Academic Data loaded: %d students, %d faculty, %d courses, %d marks, %d attendance",
                  data_get_student_count(), data_get_faculty_count(),
                  data_get_course_count(), data_get_marks_count(), data_get_attendance_count());

    /* 4. Initialize shared response queue & response dispatcher thread */
    if (response_queue_init() != 0) {
        logger_server("ERROR", "Failed to initialize response queue");
        return -1;
    }
    if (response_dispatcher_start() != 0) {
        logger_server("ERROR", "Failed to start response dispatcher thread");
        return -1;
    }

    /* 5. Initialize worker thread pool */
    if (thread_pool_init(NUM_WORKERS) != 0) {
        logger_server("ERROR", "Failed to initialize worker thread pool");
        return -1;
    }

    /* 6. Create IPv4 TCP socket (POSIX Socket API) */
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket failed");
        logger_server("ERROR", "Failed to create socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(g_config.port);

    if (bind(g_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        logger_server("ERROR", "Failed to bind to port %d", g_config.port);
        close(g_server_fd);
        return -1;
    }

    if (listen(g_server_fd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("listen failed");
        logger_server("ERROR", "Failed to listen on socket");
        close(g_server_fd);
        return -1;
    }

    /* 7. Register Signal Handlers (SIGINT, SIGTERM for shutdown, SIGHUP for reload) */
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGTERM, &sa_int, NULL);

    struct sigaction sa_hup;
    memset(&sa_hup, 0, sizeof(sa_hup));
    sa_hup.sa_handler = sighup_handler;
    sa_hup.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa_hup, NULL);

    logger_server("INFO", "=================================================");
    logger_server("INFO", "University ERP — Concurrent C Linux Server");
    logger_server("INFO", "Phase 4: Advanced OS Features (mmap, Response Queue, /proc, SIGHUP)");
    logger_server("INFO", "Listening on http://0.0.0.0:%d", g_config.port);
    logger_server("INFO", "Server PID: %d (check with ps or /proc/%d)", g_stats.pid, g_stats.pid);
    logger_server("INFO", "Worker Threads: %d | Response Queue: 32 items", NUM_WORKERS);
    logger_server("INFO", "Persistent File: %s (mmap backed)", g_config.data_file);
    logger_server("INFO", "=================================================");

    return g_server_fd;
}

void server_run(int server_fd) {
    logger_server("INFO", "[PRODUCER] Main thread accept() loop running. Piping requests to worker pool.");

    while (g_stats.is_running) {
        /* Check if SIGHUP was caught to reload configuration */
        if (g_sighup_received) {
            g_sighup_received = 0;
            config_reload();
            logger_server("INFO", "[SIGHUP] Signal caught by main thread. server.conf reloaded live!");
        }

        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (errno == EINTR) {
                /* Interrupted by signal like SIGHUP - continue */
                continue;
            }
            if (!g_stats.is_running) break;
            perror("accept error");
            continue;
        }

        char client_ip[64] = "unknown";
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        pthread_mutex_lock(&g_stats_mutex);
        g_stats.active_clients++;
        g_stats.total_requests++;
        pthread_mutex_unlock(&g_stats_mutex);

        logger_server("DEBUG", "[PRODUCER] Accepted FD=%d from %s:%d -> Request Queue",
                      client_fd, client_ip, client_port);

        /* Enqueue request into shared bounded request queue (Producer-Consumer pattern) */
        if (thread_pool_enqueue(client_fd, client_ip, client_port) != 0) {
            logger_server("ERROR", "[PRODUCER] Request queue full or server shutting down. Dropping FD=%d", client_fd);
            close(client_fd);
            pthread_mutex_lock(&g_stats_mutex);
            g_stats.failed_requests++;
            if (g_stats.active_clients > 0) g_stats.active_clients--;
            pthread_mutex_unlock(&g_stats_mutex);
        }
    }

    server_log("INFO", "[PRODUCER] Main accept loop terminated. Stopping server...");
    server_stop();
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            port = DEFAULT_PORT;
        }
    }

    int server_fd = server_init(port);
    if (server_fd < 0) {
        fprintf(stderr, "Fatal: could not start server on port %d\n", port);
        return 1;
    }

    server_run(server_fd);

    return 0;
}
