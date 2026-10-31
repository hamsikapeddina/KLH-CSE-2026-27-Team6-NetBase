#include "response_queue.h"
#include "logger.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

ResponseQueue g_response_queue;
static pthread_t g_dispatcher_thread;
static int g_dispatcher_running = 0;

int response_queue_init(void) {
    memset(&g_response_queue, 0, sizeof(ResponseQueue));
    g_response_queue.head = 0;
    g_response_queue.tail = 0;
    g_response_queue.count = 0;
    g_response_queue.shutdown = 0;

    if (pthread_mutex_init(&g_response_queue.mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&g_response_queue.cond_not_empty, NULL) != 0) return -1;
    if (pthread_cond_init(&g_response_queue.cond_not_full, NULL) != 0) return -1;

    return 0;
}

int response_queue_enqueue(const HttpResponseItem *item) {
    if (!item) return -1;

    pthread_mutex_lock(&g_response_queue.mutex);

    while (g_response_queue.count == RESPONSE_QUEUE_CAPACITY && !g_response_queue.shutdown) {
        pthread_cond_wait(&g_response_queue.cond_not_full, &g_response_queue.mutex);
    }

    if (g_response_queue.shutdown) {
        pthread_mutex_unlock(&g_response_queue.mutex);
        return -1;
    }

    g_response_queue.items[g_response_queue.tail] = *item;
    g_response_queue.tail = (g_response_queue.tail + 1) % RESPONSE_QUEUE_CAPACITY;
    g_response_queue.count++;

    pthread_cond_signal(&g_response_queue.cond_not_empty);
    pthread_mutex_unlock(&g_response_queue.mutex);

    return 0;
}

int response_queue_dequeue(HttpResponseItem *item) {
    if (!item) return -1;

    pthread_mutex_lock(&g_response_queue.mutex);

    while (g_response_queue.count == 0 && !g_response_queue.shutdown) {
        pthread_cond_wait(&g_response_queue.cond_not_empty, &g_response_queue.mutex);
    }

    if (g_response_queue.shutdown && g_response_queue.count == 0) {
        pthread_mutex_unlock(&g_response_queue.mutex);
        return -1;
    }

    *item = g_response_queue.items[g_response_queue.head];
    g_response_queue.head = (g_response_queue.head + 1) % RESPONSE_QUEUE_CAPACITY;
    g_response_queue.count--;

    pthread_cond_signal(&g_response_queue.cond_not_full);
    pthread_mutex_unlock(&g_response_queue.mutex);

    return 0;
}

void response_queue_shutdown(void) {
    pthread_mutex_lock(&g_response_queue.mutex);
    g_response_queue.shutdown = 1;
    pthread_cond_broadcast(&g_response_queue.cond_not_empty);
    pthread_cond_broadcast(&g_response_queue.cond_not_full);
    pthread_mutex_unlock(&g_response_queue.mutex);
}

void response_queue_destroy(void) {
    pthread_mutex_destroy(&g_response_queue.mutex);
    pthread_cond_destroy(&g_response_queue.cond_not_empty);
    pthread_cond_destroy(&g_response_queue.cond_not_full);
}

int response_queue_get_count(void) {
    pthread_mutex_lock(&g_response_queue.mutex);
    int c = g_response_queue.count;
    pthread_mutex_unlock(&g_response_queue.mutex);
    return c;
}

int response_queue_size(void) {
    return response_queue_get_count();
}

/* Dispatcher Thread Routine: Sole consumer of response queue */
static void *response_dispatcher_thread(void *arg) {
    (void)arg;
    logger_server("INFO", "[DISPATCHER] Dedicated response dispatcher thread online (TID: %lu)",
                  (unsigned long)pthread_self());

    HttpResponseItem item;
    while (response_queue_dequeue(&item) == 0) {
        int fd = item.client_fd;
        if (fd < 0) continue;

        if (item.is_file) {
            /* Serve static file directly from disk */
            int file_fd = open(item.file_path, O_RDONLY);
            if (file_fd < 0) {
                /* Send 404 header */
                const char *err_resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                ssize_t w = write(fd, err_resp, strlen(err_resp));
                (void)w;
            } else {
                struct stat st;
                fstat(file_fd, &st);
                char header[1024];
                int hlen = snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Server: University-ERP-POSIX-C/1.0\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %ld\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: close\r\n\r\n",
                    item.content_type, (long)st.st_size);

                ssize_t hw = write(fd, header, hlen);
                (void)hw;

                char chunk[8192];
                ssize_t n;
                while ((n = read(file_fd, chunk, sizeof(chunk))) > 0) {
                    ssize_t written = 0;
                    while (written < n) {
                        ssize_t w = write(fd, chunk + written, n - written);
                        if (w <= 0) break;
                        written += w;
                    }
                }
                close(file_fd);
            }
        } else {
            /* Dedicated concurrency demo pacing: allows legitimate temporary backlog in bounded response queue to be observable */
            if (strcmp(item.operation, "concurrency_demo") == 0) {
                usleep(22000); /* 22ms controlled dispatch pacing */
            }

            /* Send memory response: HTTP header + Body */
            char header[1024];
            int hlen = snprintf(header, sizeof(header),
                "HTTP/1.1 %d %s\r\n"
                "Server: University-ERP-POSIX-C/1.0\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE\r\n"
                "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                "Connection: close\r\n\r\n",
                item.status_code, item.status_text,
                item.content_type[0] ? item.content_type : "application/json; charset=utf-8",
                item.body_len);

            ssize_t hw = write(fd, header, hlen);
            (void)hw;
            if (item.body_len > 0) {
                ssize_t written = 0;
                while (written < (ssize_t)item.body_len) {
                    ssize_t w = write(fd, item.body + written, item.body_len - written);
                    if (w <= 0) break;
                    written += w;
                }
            }
        }

        /* Crucial: Dispatcher owns the FD and closes it here! */
        close(fd);

        /* Audit logging */
        logger_server("DEBUG", "[DISPATCHER] Sent HTTP %d to FD=%d (%s) in %.2fms",
                      item.status_code, fd, item.endpoint, item.duration_ms);

        if (item.operation[0] != '\0') {
            int is_success = (item.status_code >= 200 && item.status_code < 300);
            logger_transaction(item.worker_id, item.operation, item.record_id, is_success, item.duration_ms, item.endpoint);
        }
    }

    logger_server("INFO", "[DISPATCHER] Response dispatcher thread exited cleanly.");
    return NULL;
}

int response_dispatcher_start(void) {
    if (g_dispatcher_running) return 0;
    if (pthread_create(&g_dispatcher_thread, NULL, response_dispatcher_thread, NULL) != 0) {
        perror("pthread_create dispatcher failed");
        return -1;
    }
    g_dispatcher_running = 1;
    return 0;
}

void response_dispatcher_join(void) {
    if (g_dispatcher_running) {
        response_queue_shutdown();
        pthread_join(g_dispatcher_thread, NULL);
        g_dispatcher_running = 0;
    }
}
