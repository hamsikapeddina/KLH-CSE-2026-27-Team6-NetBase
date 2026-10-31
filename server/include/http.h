#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_MAX_METHOD_LEN 32
#define HTTP_MAX_PATH_LEN 512
#define HTTP_MAX_BODY_LEN 4096

typedef struct {
    char method[HTTP_MAX_METHOD_LEN];
    char path[HTTP_MAX_PATH_LEN];
    char query[HTTP_MAX_PATH_LEN];
    char body[HTTP_MAX_BODY_LEN];
    int content_length;
    int client_fd;
    char client_ip[64];
    int client_port;
    int worker_id; /* ID of worker thread processing this request */
} HttpRequest;

/* HTTP handling functions */
int http_parse_request(const char *raw_request, int raw_len, HttpRequest *req);
void http_handle_request(HttpRequest *req);
void http_send_response(int client_fd, int status_code, const char *status_text, 
                        const char *content_type, const char *body, size_t body_len);
void http_send_json(int client_fd, int status_code, const char *json_body);
void http_send_demo_json(int client_fd, int worker_id, int status_code, const char *json_body);
void http_send_error(int client_fd, int status_code, const char *message);
void http_serve_file(int client_fd, const char *file_path);
const char* http_get_mime_type(const char *path);

/* Worker-level request execution entry point */
void process_client_request(int worker_id, int client_fd, const char *client_ip, int client_port);

#endif /* HTTP_H */
