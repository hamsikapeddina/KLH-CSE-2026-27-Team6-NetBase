#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define DEFAULT_PORT 3000
#define BUFFER_SIZE 8192
#define MAX_PENDING_CONNECTIONS 64
#define WEB_ROOT "web"
#define LOG_FILE "server/logs/server.log"

/* Server runtime statistics (Thread-safe shared counters) */
typedef struct {
    pid_t pid;
    int port;
    time_t start_time;
    unsigned long total_requests;
    unsigned long completed_requests;
    unsigned long failed_requests;
    int active_clients;
    int pending_requests;
    int worker_count;
    int busy_workers;
    int idle_workers;
    int queue_capacity;
    int current_queue_size;
    int is_running;
} ServerStats;

extern ServerStats g_stats;

/* Server functions */
int server_init(int port);
void server_run(int server_fd);
void server_stop(void);
void server_log(const char *level, const char *fmt, ...);

#endif /* SERVER_H */
