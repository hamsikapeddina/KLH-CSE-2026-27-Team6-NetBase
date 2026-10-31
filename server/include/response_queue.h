#ifndef RESPONSE_QUEUE_H
#define RESPONSE_QUEUE_H

#include <pthread.h>
#include <stddef.h>

#define RESPONSE_QUEUE_CAPACITY 32
#define MAX_RESPONSE_BUF (64 * 1024)

/* Response Item: Transferred from Worker Thread to Response Dispatcher */
typedef struct {
    int client_fd;
    char client_ip[64];
    int client_port;
    int worker_id;
    int status_code;
    char status_text[32];
    char content_type[64];
    int is_file;
    char file_path[1024];
    size_t body_len;
    char body[MAX_RESPONSE_BUF];
    char endpoint[128];
    char operation[32];
    char record_id[32];
    double duration_ms;
} HttpResponseItem;

/* Bounded FIFO Response Queue */
typedef struct {
    HttpResponseItem items[RESPONSE_QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
} ResponseQueue;

extern ResponseQueue g_response_queue;

int response_queue_init(void);
int response_queue_enqueue(const HttpResponseItem *item);
int response_queue_dequeue(HttpResponseItem *item);
void response_queue_shutdown(void);
void response_queue_destroy(void);
int response_queue_get_count(void);
int response_queue_size(void);

/* Dedicated Response Dispatcher Thread */
int response_dispatcher_start(void);
void response_dispatcher_join(void);

#endif /* RESPONSE_QUEUE_H */
