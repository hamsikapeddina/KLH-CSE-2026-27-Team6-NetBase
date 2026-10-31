#include "http.h"
#include "server.h"
#include "data.h"
#include "pool.h"
#include "response_queue.h"
#include "mmap_data.h"
#include "proc_monitor.h"
#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>

/* Simple helper to extract a parameter value from query string (e.g., "id=S101&role=student") */
static int query_get_param(const char *query, const char *key, char *out, size_t max_out) {
    if (!query || !key || !out || max_out == 0) return -1;
    out[0] = '\0';

    size_t key_len = strlen(key);
    const char *p = query;

    while (*p) {
        if ((p == query || *(p - 1) == '&') && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val_start = p + key_len + 1;
            const char *val_end = strchr(val_start, '&');
            size_t val_len = val_end ? (size_t)(val_end - val_start) : strlen(val_start);
            if (val_len >= max_out) val_len = max_out - 1;
            strncpy(out, val_start, val_len);
            out[val_len] = '\0';
            return 0;
        }
        p++;
    }
    return -1;
}

/* Simple helper to extract string value from JSON payload */
static int json_get_string(const char *json, const char *key, char *out, size_t max_out) {
    if (!json || !key || !out || max_out == 0) return -1;
    out[0] = '\0';

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *pos = strstr(json, search_pattern);
    if (!pos) return -1;

    pos += strlen(search_pattern);
    /* Skip whitespace and colon */
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':')) pos++;

    if (*pos != '\"') return -1;
    pos++; /* skip opening quote */

    const char *closing_quote = strchr(pos, '\"');
    if (!closing_quote) return -1;

    size_t len = (size_t)(closing_quote - pos);
    if (len >= max_out) len = max_out - 1;

    strncpy(out, pos, len);
    out[len] = '\0';
    return 0;
}

/* Simple helper to extract float value from JSON payload */
static int json_get_float(const char *json, const char *key, float *out) {
    if (!json || !key || !out) return -1;

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *pos = strstr(json, search_pattern);
    if (!pos) return -1;

    pos += strlen(search_pattern);
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':')) pos++;

    if (*pos == '\0') return -1;
    *out = (float)atof(pos);
    return 0;
}

/* Simple helper to extract int value from JSON payload */
static int json_get_int(const char *json, const char *key, int *out) {
    if (!json || !key || !out) return -1;

    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *pos = strstr(json, search_pattern);
    if (!pos) return -1;

    pos += strlen(search_pattern);
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':')) pos++;

    if (*pos == '\0') return -1;
    *out = atoi(pos);
    return 0;
}

const char* http_get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0) return "image/x-icon";
    return "text/plain; charset=utf-8";
}

int http_parse_request(const char *raw_request, int raw_len, HttpRequest *req) {
    if (!raw_request || raw_len <= 0 || !req) return -1;

    memset(req->method, 0, sizeof(req->method));
    memset(req->path, 0, sizeof(req->path));
    memset(req->query, 0, sizeof(req->query));
    memset(req->body, 0, sizeof(req->body));
    req->content_length = 0;

    /* Parse request line: METHOD PATH HTTP/1.x */
    char method[32] = {0};
    char full_path[512] = {0};
    if (sscanf(raw_request, "%31s %511s", method, full_path) < 2) {
        return -1;
    }

    snprintf(req->method, sizeof(req->method), "%s", method);

    /* Separate path and query string */
    char *qmark = strchr(full_path, '?');
    if (qmark) {
        *qmark = '\0';
        snprintf(req->path, sizeof(req->path), "%s", full_path);
        snprintf(req->query, sizeof(req->query), "%s", qmark + 1);
    } else {
        snprintf(req->path, sizeof(req->path), "%s", full_path);
    }

    /* Check for Content-Length header */
    const char *cl_header = strstr(raw_request, "Content-Length:");
    if (!cl_header) {
        cl_header = strstr(raw_request, "content-length:");
    }
    if (cl_header) {
        sscanf(cl_header + 15, "%d", &req->content_length);
    }

    /* Extract body if present (separated by \r\n\r\n or \n\n) */
    const char *body_start = strstr(raw_request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
    } else {
        body_start = strstr(raw_request, "\n\n");
        if (body_start) body_start += 2;
    }

    if (body_start) {
        size_t available_body_len = raw_len - (body_start - raw_request);
        size_t to_copy = available_body_len;
        if (req->content_length > 0 && (size_t)req->content_length < to_copy) {
            to_copy = req->content_length;
        }
        if (to_copy >= sizeof(req->body)) {
            to_copy = sizeof(req->body) - 1;
        }
        memcpy(req->body, body_start, to_copy);
        req->body[to_copy] = '\0';
    }

    return 0;
}

void http_send_response(int client_fd, int status_code, const char *status_text, 
                        const char *content_type, const char *body, size_t body_len) {
    HttpResponseItem item;
    memset(&item, 0, sizeof(item));
    item.client_fd = client_fd;
    item.status_code = status_code;
    strncpy(item.status_text, status_text ? status_text : "OK", sizeof(item.status_text) - 1);
    strncpy(item.content_type, content_type ? content_type : "text/plain; charset=utf-8", sizeof(item.content_type) - 1);
    item.is_file = 0;
    if (body && body_len > 0) {
        if (body_len >= sizeof(item.body)) body_len = sizeof(item.body) - 1;
        memcpy(item.body, body, body_len);
        item.body[body_len] = '\0';
        item.body_len = body_len;
    }

    /* Enqueue to shared response queue; dispatcher thread consumes, writes, and closes FD */
    if (response_queue_enqueue(&item) != 0) {
        /* Fallback if response queue shutdown or failed */
        close(client_fd);
    }
}

void http_send_json(int client_fd, int status_code, const char *json_body) {
    const char *status_text = "OK";
    if (status_code == 201) status_text = "Created";
    else if (status_code == 400) status_text = "Bad Request";
    else if (status_code == 401) status_text = "Unauthorized";
    else if (status_code == 404) status_text = "Not Found";
    else if (status_code == 500) status_text = "Internal Server Error";

    http_send_response(client_fd, status_code, status_text, 
                       "application/json; charset=utf-8", 
                       json_body, strlen(json_body));
}

void http_send_demo_json(int client_fd, int worker_id, int status_code, const char *json_body) {
    HttpResponseItem item;
    memset(&item, 0, sizeof(item));
    item.client_fd = client_fd;
    item.worker_id = worker_id;
    item.status_code = status_code;
    strncpy(item.status_text, "OK", sizeof(item.status_text) - 1);
    strncpy(item.content_type, "application/json; charset=utf-8", sizeof(item.content_type) - 1);
    strncpy(item.endpoint, "/api/concurrency/test", sizeof(item.endpoint) - 1);
    strncpy(item.operation, "concurrency_demo", sizeof(item.operation) - 1);
    item.is_file = 0;
    size_t body_len = strlen(json_body);
    if (body_len >= sizeof(item.body)) body_len = sizeof(item.body) - 1;
    memcpy(item.body, json_body, body_len);
    item.body[body_len] = '\0';
    item.body_len = body_len;

    if (response_queue_enqueue(&item) != 0) {
        close(client_fd);
    }
}

void http_send_error(int client_fd, int status_code, const char *message) {
    char error_json[512];
    snprintf(error_json, sizeof(error_json), 
             "{\"error\": true, \"status\": %d, \"message\": \"%s\"}", 
             status_code, message);
    http_send_json(client_fd, status_code, error_json);
}

void http_serve_file(int client_fd, const char *file_path) {
    char resolved_path[1024];

    /* Default root request serves index.html */
    if (strcmp(file_path, "/") == 0 || strlen(file_path) == 0) {
        snprintf(resolved_path, sizeof(resolved_path), "%s/index.html", WEB_ROOT);
    } else {
        /* Strip leading slash for relative web root lookup */
        const char *rel_path = file_path;
        if (rel_path[0] == '/') rel_path++;
        snprintf(resolved_path, sizeof(resolved_path), "%s/%s", WEB_ROOT, rel_path);
    }

    /* Check if file exists */
    struct stat st;
    if (stat(resolved_path, &st) < 0 || S_ISDIR(st.st_mode)) {
        server_log("WARN", "File not found: %s (requested: %s)", resolved_path, file_path);
        http_send_error(client_fd, 404, "Requested resource not found on C server");
        return;
    }

    const char *mime_type = http_get_mime_type(resolved_path);

    HttpResponseItem item;
    memset(&item, 0, sizeof(item));
    item.client_fd = client_fd;
    item.status_code = 200;
    strncpy(item.status_text, "OK", sizeof(item.status_text) - 1);
    strncpy(item.content_type, mime_type, sizeof(item.content_type) - 1);
    item.is_file = 1;
    snprintf(item.file_path, sizeof(item.file_path), "%s", resolved_path);
    snprintf(item.endpoint, sizeof(item.endpoint), "%s", file_path);

    if (response_queue_enqueue(&item) != 0) {
        close(client_fd);
    }
}

void http_handle_request(HttpRequest *req) {
    if (!req) return;

    if (req->worker_id >= 0 && req->worker_id < NUM_WORKERS) {
        snprintf(g_pool.workers[req->worker_id].current_path,
                 sizeof(g_pool.workers[req->worker_id].current_path),
                 "%.32s %.400s", req->method, req->path);
    }

    server_log("INFO", "[Worker %d] Processing %s %s from %s:%d (FD=%d)", 
               req->worker_id, req->method, req->path, req->client_ip, req->client_port, req->client_fd);

    /* Handle CORS preflight */
    if (strcmp(req->method, "OPTIONS") == 0) {
        http_send_response(req->client_fd, 204, "No Content", "text/plain", "", 0);
        return;
    }

    /* ------------------------------------------------------------- */
    /* PHASE 1 & 2 FOUNDATION ENDPOINTS (Maintained & Extended)      */
    /* ------------------------------------------------------------- */

    /* API Endpoint: GET /api/health */
    if (strcmp(req->path, "/api/health") == 0) {
        char json[256];
        snprintf(json, sizeof(json),
            "{\"status\": \"UP\", \"phase\": 5, \"worker_id\": %d, \"system\": \"University ERP C Server\", \"timestamp\": %ld}",
            req->worker_id, (long)time(NULL));
        http_send_json(req->client_fd, 200, json);
        return;
    }

    /* API Endpoint: GET /api/status */
    if (strcmp(req->path, "/api/status") == 0) {
        time_t now = time(NULL);
        long uptime = (long)(now - g_stats.start_time);

        /* Read synchronized statistics */
        pthread_mutex_lock(&g_stats_mutex);
        unsigned long total_req = g_stats.total_requests;
        unsigned long comp_req = g_stats.completed_requests;
        unsigned long fail_req = g_stats.failed_requests;
        int act_clients = g_stats.active_clients;
        int pend_req = g_stats.pending_requests;
        int w_count = g_stats.worker_count;
        int busy_w = g_stats.busy_workers;
        int idle_w = g_stats.idle_workers;
        int q_cap = g_stats.queue_capacity;
        int cur_q = thread_pool_get_queue_size();
        pthread_mutex_unlock(&g_stats_mutex);

        int sem_val = 0;
        sem_getvalue(&g_data_access_sem, &sem_val);

        int resp_q_size = response_queue_size();

        ProcMetrics pm;
        proc_get_metrics(&pm);

        char json[8192];
        int offset = snprintf(json, sizeof(json),
            "{\n"
            "  \"status\": \"ONLINE\",\n"
            "  \"phase\": 5,\n"
            "  \"phase_title\": \"Phase 5: OS Monitor, Integration Testing & Demo Polish\",\n"
            "  \"server_pid\": %d,\n"
            "  \"port\": %d,\n"
            "  \"uptime_seconds\": %ld,\n"
            "  \"total_requests\": %lu,\n"
            "  \"completed_requests\": %lu,\n"
            "  \"failed_requests\": %lu,\n"
            "  \"active_clients\": %d,\n"
            "  \"pending_requests\": %d,\n"
            "  \"worker_id_responding\": %d,\n"
            "  \"thread_pool\": {\n"
            "    \"worker_count\": %d,\n"
            "    \"busy_workers\": %d,\n"
            "    \"idle_workers\": %d,\n"
            "    \"queue_capacity\": %d,\n"
            "    \"current_queue_size\": %d\n"
            "  },\n"
            "  \"response_queue\": {\n"
            "    \"capacity\": 32,\n"
            "    \"current_size\": %d,\n"
            "    \"dispatcher_active\": true\n"
            "  },\n"
            "  \"mmap_store\": {\n"
            "    \"active\": %s,\n"
            "    \"file\": \"%s\",\n"
            "    \"file_path\": \"%s\",\n"
            "    \"magic\": \"0x554E4956\",\n"
            "    \"size_bytes\": %zu,\n"
            "    \"mapped_size\": %zu,\n"
            "    \"transaction_count\": %u,\n"
            "    \"last_sync_time\": \"%s\"\n"
            "  },\n"
            "  \"mmap\": {\n"
            "    \"active\": %s,\n"
            "    \"file_path\": \"%s\",\n"
            "    \"mapped_size\": %zu,\n"
            "    \"transaction_count\": %u,\n"
            "    \"last_sync\": \"%s\"\n"
            "  },\n"
            "  \"semaphore\": {\n"
            "    \"resource\": \"Academic Data Access Slots\",\n"
            "    \"total_slots\": %d,\n"
            "    \"available_slots\": %d\n"
            "  },\n"
            "  \"proc\": {\n"
            "    \"state\": \"%c\",\n"
            "    \"state_name\": \"%s\",\n"
            "    \"num_threads\": %ld,\n"
            "    \"vmrss_kb\": %lu,\n"
            "    \"rss_kb\": %lu,\n"
            "    \"vsize_kb\": %lu,\n"
            "    \"open_fds\": %d,\n"
            "    \"cpu_time_seconds\": %.2f,\n"
            "    \"utime_ticks\": %lu,\n"
            "    \"stime_ticks\": %lu\n"
            "  },\n"
            "  \"proc_metrics\": {\n"
            "    \"state\": \"%c\",\n"
            "    \"state_name\": \"%s\",\n"
            "    \"num_threads\": %ld,\n"
            "    \"rss_kb\": %lu,\n"
            "    \"vmrss_kb\": %lu,\n"
            "    \"vsize_kb\": %lu,\n"
            "    \"open_fds\": %d,\n"
            "    \"cpu_time_seconds\": %.2f,\n"
            "    \"utime_ticks\": %lu,\n"
            "    \"stime_ticks\": %lu\n"
            "  },\n"
            "  \"config\": {\n"
            "    \"file\": \"%s\",\n"
            "    \"reload_count\": %d,\n"
            "    \"last_reload_time\": \"%s\",\n"
            "    \"data_access_slots\": %d\n"
            "  },\n"
            "  \"workers\": [\n",
            g_stats.pid, g_stats.port, uptime, total_req, comp_req, fail_req,
            act_clients, pend_req, req->worker_id,
            w_count, busy_w, idle_w, q_cap, cur_q,
            resp_q_size,
            mmap_data_is_active() ? "true" : "false",
            mmap_data_get_path(),
            mmap_data_get_path(),
            mmap_data_get_size(),
            mmap_data_get_size(),
            g_mmap_store ? g_mmap_store->transaction_count : 0,
            g_mmap_store ? g_mmap_store->last_sync_time : "Startup",
            mmap_data_is_active() ? "true" : "false",
            mmap_data_get_path(),
            mmap_data_get_size(),
            g_mmap_store ? g_mmap_store->transaction_count : 0,
            g_mmap_store ? g_mmap_store->last_sync_time : "Startup",
            DATA_ACCESS_SLOTS, sem_val,
            pm.state_char, pm.state_name, pm.num_threads,
            pm.vmrss_kb, pm.vmrss_kb, pm.vsize_kb, pm.open_fds, pm.cpu_time_seconds,
            pm.utime_ticks, pm.stime_ticks,
            pm.state_char, pm.state_name, pm.num_threads,
            pm.vmrss_kb, pm.vmrss_kb, pm.vsize_kb, pm.open_fds, pm.cpu_time_seconds,
            pm.utime_ticks, pm.stime_ticks,
            g_config.config_file, g_config.reload_count,
            g_config.last_reload_time[0] ? g_config.last_reload_time : "Startup",
            g_config.data_access_slots);

        for (int i = 0; i < NUM_WORKERS; i++) {
            const char *activity = g_pool.workers[i].is_busy ? 
                (g_pool.workers[i].current_path[0] ? g_pool.workers[i].current_path : "Serving Socket") : 
                "Waiting on cond_not_empty";

            offset += snprintf(json + offset, sizeof(json) - offset,
                "    {\"id\": %d, \"busy\": %s, \"tasks_completed\": %lu, \"current_activity\": \"%s\"}%s\n",
                g_pool.workers[i].id,
                g_pool.workers[i].is_busy ? "true" : "false",
                g_pool.workers[i].tasks_completed,
                activity,
                (i == NUM_WORKERS - 1) ? "" : ",");
        }

        snprintf(json + offset, sizeof(json) - offset,
            "  ],\n"
            "  \"students_count\": %d,\n"
            "  \"faculty_count\": %d,\n"
            "  \"courses_count\": %d,\n"
            "  \"compiler\": \"gcc (Ubuntu / POSIX Linux)\",\n"
            "  \"os_concepts\": [\n"
            "    \"POSIX Threads (pthread_create, pthread_join, fixed 4-worker pool)\",\n"
            "    \"Two-Stage Producer-Consumer Queue (Request Queue + Response Queue with Dedicated Dispatcher)\",\n"
            "    \"Memory-Mapped File Storage (POSIX mmap, msync, munmap binary university data store)\",\n"
            "    \"Linux /proc Virtual Filesystem (/proc/self/stat, /proc/uptime kernel metric parser)\",\n"
            "    \"Dynamic SIGHUP Configuration Reload (server.conf live reload without restart)\",\n"
            "    \"POSIX Mutex (pthread_mutex_t mutual exclusion for counters and queues)\",\n"
            "    \"POSIX Condition Variables (pthread_cond_t producer-consumer signaling)\",\n"
            "    \"POSIX Counting Semaphore (sem_init, sem_wait, sem_post DB transaction slots)\",\n"
            "    \"Thread-Safe Structured File Logging (server.log, transactions.log with transaction tracking)\",\n"
            "    \"POSIX Socket API (AF_INET, SOCK_STREAM, accept, bind, listen)\",\n"
            "    \"Signal Handling (SIGINT/SIGTERM graceful pipeline drain, SIGHUP reload)\"\n"
            "  ]\n"
            "}",
            data_get_student_count(), data_get_faculty_count(), data_get_course_count());

        http_send_json(req->client_fd, 200, json);
        return;
    }

    /* API Endpoint: GET /api/sample-data */
    if (strcmp(req->path, "/api/sample-data") == 0) {
        data_access_enter(req->worker_id, "sample_data");
        char data_json[8192];
        data_to_json(data_json, sizeof(data_json));
        data_access_exit(req->worker_id, "sample_data");
        http_send_json(req->client_fd, 200, data_json);
        return;
    }

    /* API Endpoint: POST /api/echo */
    if (strcmp(req->path, "/api/echo") == 0) {
        char echo_json[4096];
        const char *body_text = req->body[0] ? req->body : "{}";
        snprintf(echo_json, sizeof(echo_json),
            "{\n"
            "  \"success\": true,\n"
            "  \"message\": \"Browser payload received and echoed by C Linux Server\",\n"
            "  \"worker_id\": %d,\n"
            "  \"server_pid\": %d,\n"
            "  \"content_length\": %d,\n"
            "  \"received_payload\": %.*s\n"
            "}",
            req->worker_id, g_stats.pid, req->content_length, 3000, body_text);
        http_send_json(req->client_fd, 200, echo_json);
        return;
    }

    /* API Endpoint: GET /api/concurrency/test or /api/concurrency-demo */
    /* Controlled demonstration path for observable OS Request Queue, Response Queue & Semaphore */
    if (strcmp(req->path, "/api/concurrency/test") == 0 || strcmp(req->path, "/api/concurrency-demo") == 0) {
        int req_id = 1;
        char task_type[32] = "academic_tx";
        int work_delay_ms = 32; /* Controlled processing delay inside database semaphore section */

        if (req->query[0] != '\0') {
            const char *id_param = strstr(req->query, "id=");
            if (id_param) sscanf(id_param + 3, "%d", &req_id);
            const char *type_param = strstr(req->query, "type=");
            if (type_param) sscanf(type_param + 5, "%31[^&]", task_type);
            const char *delay_param = strstr(req->query, "delay_ms=");
            if (delay_param) sscanf(delay_param + 9, "%d", &work_delay_ms);
        }
        if (work_delay_ms < 5) work_delay_ms = 5;
        if (work_delay_ms > 200) work_delay_ms = 200;

        /* 1. Update worker diagnostic activity */
        snprintf(g_pool.workers[req->worker_id].current_path,
                 sizeof(g_pool.workers[req->worker_id].current_path),
                 "Demo Req #%d (%s)", req_id, task_type);

        /* 2. Acquire real POSIX Counting Semaphore (2 DB transaction slots) */
        data_access_enter(req->worker_id, "demo_academic_tx");

        /* 3. Controlled processing delay inside critical section so worker execution time allows queue buildup */
        /* Simulates mmap academic database query & transaction processing */
        usleep(work_delay_ms * 1000);

        /* 4. Release real POSIX Counting Semaphore */
        data_access_exit(req->worker_id, "demo_academic_tx");

        /* 5. Format JSON response with real worker metadata */
        char demo_json[2048];
        snprintf(demo_json, sizeof(demo_json),
            "{\n"
            "  \"success\": true,\n"
            "  \"worker_id\": %d,\n"
            "  \"task_id\": %d,\n"
            "  \"task_type\": \"%s\",\n"
            "  \"endpoint\": \"%.256s\",\n"
            "  \"semaphore_slot_held\": true,\n"
            "  \"status\": \"PROCESSED_BY_POSIX_WORKER\",\n"
            "  \"timestamp\": %ld\n"
            "}",
            req->worker_id, req_id, task_type, req->path, (long)time(NULL));

        /* 6. Enqueue into bounded Response Queue with operation='concurrency_demo' */
        http_send_demo_json(req->client_fd, req->worker_id, 200, demo_json);
        return;
    }

    /* ------------------------------------------------------------- */
    /* PHASE 2 & 3 ERP CORE ENDPOINTS                                */
    /* ------------------------------------------------------------- */

    /* API Endpoint: POST /api/auth/login */
    if (strcmp(req->path, "/api/auth/login") == 0) {
        char role[32] = {0};
        char id[32] = {0};
        char password[64] = {0};

        json_get_string(req->body, "role", role, sizeof(role));
        json_get_string(req->body, "id", id, sizeof(id));
        json_get_string(req->body, "password", password, sizeof(password));

        data_access_enter(req->worker_id, "auth_login");
        void *user_ptr = NULL;
        int auth_res = data_authenticate(role, id, password, &user_ptr);
        data_access_exit(req->worker_id, "auth_login");

        if (auth_res == 0 && user_ptr) {
            char resp[1024];
            if (strcmp(role, "student") == 0) {
                Student *s = (Student*)user_ptr;
                snprintf(resp, sizeof(resp),
                    "{\n"
                    "  \"success\": true,\n"
                    "  \"role\": \"student\",\n"
                    "  \"worker_id\": %d,\n"
                    "  \"user\": {\n"
                    "    \"id\": \"%s\",\n"
                    "    \"name\": \"%s\",\n"
                    "    \"email\": \"%s\",\n"
                    "    \"department\": \"%s\",\n"
                    "    \"year\": %d,\n"
                    "    \"section\": \"%s\"\n"
                    "  }\n"
                    "}",
                    req->worker_id, s->id, s->name, s->email, s->department, s->year, s->section);
            } else {
                Faculty *f = (Faculty*)user_ptr;
                snprintf(resp, sizeof(resp),
                    "{\n"
                    "  \"success\": true,\n"
                    "  \"role\": \"faculty\",\n"
                    "  \"worker_id\": %d,\n"
                    "  \"user\": {\n"
                    "    \"id\": \"%s\",\n"
                    "    \"name\": \"%s\",\n"
                    "    \"email\": \"%s\",\n"
                    "    \"department\": \"%s\"\n"
                    "  }\n"
                    "}",
                    req->worker_id, f->id, f->name, f->email, f->department);
            }
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 401, "Invalid credentials. Please check ID and password.");
        }
        return;
    }

    /* API Endpoint: GET /api/student/dashboard?id=S101 */
    if (strcmp(req->path, "/api/student/dashboard") == 0) {
        char student_id[32] = {0};
        query_get_param(req->query, "id", student_id, sizeof(student_id));
        if (strlen(student_id) == 0) {
            strcpy(student_id, "S101");
        }

        data_access_enter(req->worker_id, "student_dashboard");
        char buffer[12288];
        data_student_dashboard_to_json(student_id, buffer, sizeof(buffer));
        data_access_exit(req->worker_id, "student_dashboard");

        http_send_json(req->client_fd, 200, buffer);
        return;
    }

    /* API Endpoint: GET /api/faculty/dashboard?id=F201 */
    if (strcmp(req->path, "/api/faculty/dashboard") == 0) {
        char faculty_id[32] = {0};
        query_get_param(req->query, "id", faculty_id, sizeof(faculty_id));
        if (strlen(faculty_id) == 0) {
            strcpy(faculty_id, "F201");
        }

        data_access_enter(req->worker_id, "faculty_dashboard");
        char buffer[16384];
        data_faculty_dashboard_to_json(faculty_id, buffer, sizeof(buffer));
        data_access_exit(req->worker_id, "faculty_dashboard");

        http_send_json(req->client_fd, 200, buffer);
        return;
    }

    /* API Endpoint: POST /api/marks/update */
    if (strcmp(req->path, "/api/marks/update") == 0) {
        char student_id[32] = {0};
        char course_id[32] = {0};
        float midterm = 0.0f;
        float endterm = 0.0f;
        float quiz = 0.0f;

        json_get_string(req->body, "student_id", student_id, sizeof(student_id));
        json_get_string(req->body, "course_id", course_id, sizeof(course_id));
        json_get_float(req->body, "midterm", &midterm);
        json_get_float(req->body, "endterm", &endterm);
        json_get_float(req->body, "quiz", &quiz);

        if (strlen(student_id) == 0 || strlen(course_id) == 0) {
            http_send_error(req->client_fd, 400, "Missing student_id or course_id");
            return;
        }

        data_access_enter(req->worker_id, "marks_update");
        MarkRecord updated;
        int res = data_update_marks(student_id, course_id, midterm, endterm, quiz, &updated);
        data_access_exit(req->worker_id, "marks_update");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\n"
                "  \"success\": true,\n"
                "  \"message\": \"Marks updated successfully in C memory store\",\n"
                "  \"worker_id\": %d,\n"
                "  \"record\": {\n"
                "    \"student_id\": \"%s\",\n"
                "    \"course_id\": \"%s\",\n"
                "    \"midterm\": %.1f,\n"
                "    \"endterm\": %.1f,\n"
                "    \"quiz\": %.1f,\n"
                "    \"total_pct\": %.2f,\n"
                "    \"grade\": \"%s\"\n"
                "  }\n"
                "}",
                req->worker_id, updated.student_id, updated.course_id, updated.midterm,
                updated.endterm, updated.quiz, updated.total_pct, updated.grade);
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 500, "Failed to update marks");
        }
        return;
    }

    /* API Endpoint: POST /api/attendance/update */
    if (strcmp(req->path, "/api/attendance/update") == 0) {
        char student_id[32] = {0};
        char course_id[32] = {0};
        int attended = 0;
        int total = 0;

        json_get_string(req->body, "student_id", student_id, sizeof(student_id));
        json_get_string(req->body, "course_id", course_id, sizeof(course_id));
        json_get_int(req->body, "attended", &attended);
        json_get_int(req->body, "total", &total);

        if (strlen(student_id) == 0 || strlen(course_id) == 0) {
            http_send_error(req->client_fd, 400, "Missing student_id or course_id");
            return;
        }

        data_access_enter(req->worker_id, "attendance_update");
        AttendanceRecord updated;
        int res = data_update_attendance(student_id, course_id, attended, total, &updated);
        data_access_exit(req->worker_id, "attendance_update");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\n"
                "  \"success\": true,\n"
                "  \"message\": \"Attendance updated successfully in C memory store\",\n"
                "  \"worker_id\": %d,\n"
                "  \"record\": {\n"
                "    \"student_id\": \"%s\",\n"
                "    \"course_id\": \"%s\",\n"
                "    \"attended\": %d,\n"
                "    \"total\": %d,\n"
                "    \"percentage\": %.1f\n"
                "  }\n"
                "}",
                req->worker_id, updated.student_id, updated.course_id, updated.attended_classes,
                updated.total_classes, updated.percentage);
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 500, "Failed to update attendance");
        }
        return;
    }

    /* API Endpoint: GET /api/courses */
    if (strcmp(req->path, "/api/courses") == 0) {
        data_access_enter(req->worker_id, "get_courses");
        const Course *courses = data_get_courses();
        int count = data_get_course_count();
        char resp[4096];
        size_t off = 0;
        off += snprintf(resp + off, sizeof(resp) - off, "{\"courses\": [\n");
        for (int i = 0; i < count; i++) {
            const Faculty *f = data_find_faculty_by_id(courses[i].faculty_id);
            off += snprintf(resp + off, sizeof(resp) - off,
                "  {\"id\": \"%s\", \"name\": \"%s\", \"credits\": %d, \"department\": \"%s\", \"semester\": %d, \"faculty_name\": \"%s\"}%s\n",
                courses[i].id, courses[i].name, courses[i].credits, courses[i].department,
                courses[i].semester, f ? f->name : "N/A", (i == count - 1) ? "" : ",");
        }
        snprintf(resp + off, sizeof(resp) - off, "]}");
        data_access_exit(req->worker_id, "get_courses");

        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: GET /api/student?id=S101 or GET /api/student/search?q=query */
    if (strcmp(req->path, "/api/student") == 0 || strcmp(req->path, "/api/student/search") == 0) {
        char id[32] = {0};
        char query[64] = {0};
        query_get_param(req->query, "id", id, sizeof(id));
        query_get_param(req->query, "q", query, sizeof(query));
        if (strlen(id) == 0 && strlen(query) == 0) {
            http_send_error(req->client_fd, 400, "Missing id or q search parameter");
            return;
        }

        const char *target = (strlen(id) > 0) ? id : query;
        data_access_enter(req->worker_id, "student_search");
        const Student *s = data_find_student_by_id(target);
        if (!s) {
            const Student *all = data_get_students();
            int count = data_get_student_count();
            for (int i = 0; i < count; i++) {
                if (strstr(all[i].name, target) != NULL || strcmp(all[i].id, target) == 0) {
                    s = &all[i];
                    break;
                }
            }
        }

        if (s) {
            char resp[1024];
            snprintf(resp, sizeof(resp),
                "{\n"
                "  \"success\": true,\n"
                "  \"found\": true,\n"
                "  \"worker_id\": %d,\n"
                "  \"student\": {\n"
                "    \"id\": \"%s\",\n"
                "    \"name\": \"%s\",\n"
                "    \"email\": \"%s\",\n"
                "    \"department\": \"%s\",\n"
                "    \"year\": %d,\n"
                "    \"section\": \"%s\"\n"
                "  }\n"
                "}",
                req->worker_id, s->id, s->name, s->email, s->department, s->year, s->section);
            data_access_exit(req->worker_id, "student_search");
            http_send_json(req->client_fd, 200, resp);
        } else {
            data_access_exit(req->worker_id, "student_search");
            http_send_json(req->client_fd, 404, "{\"success\": false, \"found\": false, \"message\": \"Student not found\"}");
        }
        return;
    }

    /* API Endpoint: GET /api/students */
    if (strcmp(req->path, "/api/students") == 0) {
        data_access_enter(req->worker_id, "get_students");
        const Student *students = data_get_students();
        int count = data_get_student_count();
        char resp[4096];
        size_t off = 0;
        off += snprintf(resp + off, sizeof(resp) - off, "{\"students\": [\n");
        for (int i = 0; i < count; i++) {
            off += snprintf(resp + off, sizeof(resp) - off,
                "  {\"id\": \"%s\", \"name\": \"%s\", \"email\": \"%s\", \"department\": \"%s\", \"year\": %d, \"section\": \"%s\"}%s\n",
                students[i].id, students[i].name, students[i].email, students[i].department,
                students[i].year, students[i].section, (i == count - 1) ? "" : ",");
        }
        snprintf(resp + off, sizeof(resp) - off, "]}");
        data_access_exit(req->worker_id, "get_students");

        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: GET /api/faculty or GET /api/faculty/search?q=... */
    if (strcmp(req->path, "/api/faculty") == 0 || strcmp(req->path, "/api/faculty/search") == 0) {
        char id[32] = {0};
        char query[64] = {0};
        query_get_param(req->query, "id", id, sizeof(id));
        query_get_param(req->query, "q", query, sizeof(query));

        if (strlen(id) > 0 || strlen(query) > 0) {
            const char *target = (strlen(id) > 0) ? id : query;
            data_access_enter(req->worker_id, "faculty_search");
            const Faculty *f = data_find_faculty_by_id(target);
            if (!f) {
                const Faculty *all = data_get_faculty();
                int count = data_get_faculty_count();
                for (int i = 0; i < count; i++) {
                    if (strstr(all[i].name, target) != NULL || strcmp(all[i].id, target) == 0) {
                        f = &all[i];
                        break;
                    }
                }
            }

            if (f) {
                char resp[1024];
                snprintf(resp, sizeof(resp),
                    "{\n"
                    "  \"success\": true,\n"
                    "  \"found\": true,\n"
                    "  \"worker_id\": %d,\n"
                    "  \"faculty\": {\n"
                    "    \"id\": \"%s\",\n"
                    "    \"name\": \"%s\",\n"
                    "    \"email\": \"%s\",\n"
                    "    \"department\": \"%s\"\n"
                    "  }\n"
                    "}",
                    req->worker_id, f->id, f->name, f->email, f->department);
                data_access_exit(req->worker_id, "faculty_search");
                http_send_json(req->client_fd, 200, resp);
            } else {
                data_access_exit(req->worker_id, "faculty_search");
                http_send_json(req->client_fd, 404, "{\"success\": false, \"found\": false, \"message\": \"Faculty not found\"}");
            }
            return;
        }

        data_access_enter(req->worker_id, "get_faculty");
        const Faculty *faculty = data_get_faculty();
        int count = data_get_faculty_count();
        char resp[4096];
        size_t off = 0;
        off += snprintf(resp + off, sizeof(resp) - off, "{\"faculty\": [\n");
        for (int i = 0; i < count; i++) {
            off += snprintf(resp + off, sizeof(resp) - off,
                "  {\"id\": \"%s\", \"name\": \"%s\", \"email\": \"%s\", \"department\": \"%s\"}%s\n",
                faculty[i].id, faculty[i].name, faculty[i].email, faculty[i].department, (i == count - 1) ? "" : ",");
        }
        snprintf(resp + off, sizeof(resp) - off, "]}");
        data_access_exit(req->worker_id, "get_faculty");

        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* ------------------------------------------------------------- */
    /* PHASE 4 LINUX OS & MMAP EXTENSIONS                             */
    /* ------------------------------------------------------------- */

    /* API Endpoint: GET /api/proc/stats & GET /api/os/metrics */
    if (strcmp(req->path, "/api/proc/stats") == 0 || strcmp(req->path, "/api/os/metrics") == 0) {
        ProcMetrics pm;
        proc_get_metrics(&pm);
        char resp[2048];
        snprintf(resp, sizeof(resp),
            "{\n"
            "  \"pid\": %d,\n"
            "  \"ppid\": %d,\n"
            "  \"comm\": \"%s\",\n"
            "  \"state\": \"%c\",\n"
            "  \"state_name\": \"%s\",\n"
            "  \"num_threads\": %ld,\n"
            "  \"cpu_time_seconds\": %.2f,\n"
            "  \"utime_ticks\": %lu,\n"
            "  \"stime_ticks\": %lu,\n"
            "  \"vsize_kb\": %lu,\n"
            "  \"vmrss_kb\": %lu,\n"
            "  \"open_fds\": %d,\n"
            "  \"page_size\": %ld,\n"
            "  \"clock_ticks_per_sec\": %ld,\n"
            "  \"uptime_seconds\": %lu\n"
            "}",
            (int)pm.pid, (int)pm.ppid, pm.comm, pm.state_char, pm.state_name,
            pm.num_threads, pm.cpu_time_seconds, pm.utime_ticks, pm.stime_ticks,
            pm.vsize_kb, pm.vmrss_kb, pm.open_fds, pm.page_size,
            pm.clock_ticks_per_sec, pm.uptime_seconds);
        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: GET /api/mmap/info */
    if (strcmp(req->path, "/api/mmap/info") == 0) {
        char resp[2048];
        if (g_mmap_store) {
            snprintf(resp, sizeof(resp),
                "{\n"
                "  \"initialized\": true,\n"
                "  \"magic\": \"0x%X\",\n"
                "  \"magic_valid\": %s,\n"
                "  \"version\": %u,\n"
                "  \"file_path\": \"%s\",\n"
                "  \"file_size_bytes\": %zu,\n"
                "  \"last_sync_time\": \"%s\",\n"
                "  \"student_count\": %u,\n"
                "  \"faculty_count\": %u,\n"
                "  \"course_count\": %u,\n"
                "  \"marks_count\": %u,\n"
                "  \"attendance_count\": %u,\n"
                "  \"transaction_count\": %u\n"
                "}",
                g_mmap_store->magic,
                (g_mmap_store->magic == MMAP_MAGIC) ? "true" : "false",
                g_mmap_store->version,
                g_config.data_file,
                sizeof(MmapUniversityStore),
                g_mmap_store->last_sync_time,
                g_mmap_store->student_count,
                g_mmap_store->faculty_count,
                g_mmap_store->course_count,
                g_mmap_store->marks_count,
                g_mmap_store->attendance_count,
                g_mmap_store->transaction_count);
        } else {
            snprintf(resp, sizeof(resp), "{\"initialized\": false, \"message\": \"mmap store not initialized\"}");
        }
        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: GET /api/config */
    if (strcmp(req->path, "/api/config") == 0) {
        char resp[2048];
        snprintf(resp, sizeof(resp),
            "{\n"
            "  \"port\": %d,\n"
            "  \"server_name\": \"%s\",\n"
            "  \"config_file\": \"%s\",\n"
            "  \"log_enabled\": %d,\n"
            "  \"data_access_slots\": %d,\n"
            "  \"request_queue_capacity\": %d,\n"
            "  \"response_queue_capacity\": %d,\n"
            "  \"max_workers\": %d,\n"
            "  \"data_file\": \"%s\",\n"
            "  \"server_log\": \"%s\",\n"
            "  \"transactions_log\": \"%s\",\n"
            "  \"reload_count\": %d,\n"
            "  \"last_reload_time\": \"%s\"\n"
            "}",
            g_config.port, g_config.server_name, g_config.config_file,
            g_config.log_enabled, g_config.data_access_slots,
            g_config.request_queue_capacity, g_config.response_queue_capacity,
            g_config.max_workers, g_config.data_file, g_config.server_log,
            g_config.transactions_log, g_config.reload_count, g_config.last_reload_time);
        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: POST /api/config/reload (Simulates or executes SIGHUP reload) */
    if (strcmp(req->path, "/api/config/reload") == 0) {
        int res = config_reload();
        char resp[512];
        snprintf(resp, sizeof(resp),
            "{\"success\": %s, \"reload_count\": %d, \"last_reload_time\": \"%s\"}",
            (res == 0) ? "true" : "false", g_config.reload_count, g_config.last_reload_time);
        http_send_json(req->client_fd, (res == 0) ? 200 : 500, resp);
        return;
    }

    /* API Endpoint: GET /api/logs/server */
    if (strcmp(req->path, "/api/logs/server") == 0) {
        char log_buf[32 * 1024];
        logger_get_recent_server_logs(log_buf, sizeof(log_buf), 50);
        char resp[33 * 1024];
        snprintf(resp, sizeof(resp), "{\"logs\": %s}", log_buf);
        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: GET /api/logs/transactions or /api/transactions */
    if (strcmp(req->path, "/api/logs/transactions") == 0 || strcmp(req->path, "/api/transactions") == 0) {
        char tx_buf[32 * 1024];
        logger_get_recent_transactions(tx_buf, sizeof(tx_buf), 50);
        char resp[33 * 1024];
        snprintf(resp, sizeof(resp), "{\"transactions\": %s}", tx_buf);
        http_send_json(req->client_fd, 200, resp);
        return;
    }

    /* API Endpoint: POST /api/student/create or /api/students/insert */
    if (strcmp(req->path, "/api/student/create") == 0 || strcmp(req->path, "/api/students/insert") == 0) {
        Student s;
        memset(&s, 0, sizeof(s));
        json_get_string(req->body, "id", s.id, sizeof(s.id));
        json_get_string(req->body, "name", s.name, sizeof(s.name));
        json_get_string(req->body, "email", s.email, sizeof(s.email));
        json_get_string(req->body, "department", s.department, sizeof(s.department));
        json_get_int(req->body, "year", &s.year);
        json_get_string(req->body, "section", s.section, sizeof(s.section));
        json_get_string(req->body, "password", s.password, sizeof(s.password));

        if (strlen(s.id) == 0 || strlen(s.name) == 0) {
            http_send_error(req->client_fd, 400, "Missing required student fields (id, name)");
            return;
        }

        if (strlen(s.password) == 0) strcpy(s.password, "pass123");

        data_access_enter(req->worker_id, "student_insert");
        int res = data_insert_student(&s);
        data_access_exit(req->worker_id, "student_insert");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Student created and synced to mmap file\", \"student\": {\"id\": \"%s\", \"name\": \"%s\"}}",
                s.id, s.name);
            http_send_json(req->client_fd, 201, resp);
        } else {
            http_send_error(req->client_fd, 409, "Student with this ID already exists or storage full");
        }
        return;
    }

    /* API Endpoint: POST /api/student/update or /api/students/update */
    if (strcmp(req->path, "/api/student/update") == 0 || strcmp(req->path, "/api/students/update") == 0) {
        Student s;
        memset(&s, 0, sizeof(s));
        json_get_string(req->body, "id", s.id, sizeof(s.id));
        json_get_string(req->body, "name", s.name, sizeof(s.name));
        json_get_string(req->body, "email", s.email, sizeof(s.email));
        json_get_string(req->body, "department", s.department, sizeof(s.department));
        json_get_int(req->body, "year", &s.year);
        json_get_string(req->body, "section", s.section, sizeof(s.section));
        json_get_string(req->body, "password", s.password, sizeof(s.password));

        if (strlen(s.id) == 0) {
            http_send_error(req->client_fd, 400, "Missing student id");
            return;
        }

        data_access_enter(req->worker_id, "student_update");
        int res = data_update_student(&s);
        data_access_exit(req->worker_id, "student_update");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Student updated and synced to mmap file\", \"student\": {\"id\": \"%s\", \"name\": \"%s\"}}",
                s.id, s.name);
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 404, "Student not found");
        }
        return;
    }

    /* API Endpoint: POST /api/student/delete or /api/students/delete */
    if (strcmp(req->path, "/api/student/delete") == 0 || strcmp(req->path, "/api/students/delete") == 0) {
        char student_id[32] = {0};
        json_get_string(req->body, "id", student_id, sizeof(student_id));
        if (strlen(student_id) == 0) {
            query_get_param(req->query, "id", student_id, sizeof(student_id));
        }

        if (strlen(student_id) == 0) {
            http_send_error(req->client_fd, 400, "Missing student id");
            return;
        }

        data_access_enter(req->worker_id, "student_delete");
        int res = data_delete_student(student_id);
        data_access_exit(req->worker_id, "student_delete");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Student %s deleted and synced to mmap file\"}",
                student_id);
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 404, "Student not found");
        }
        return;
    }

    /* API Endpoint: POST /api/marks/delete */
    if (strcmp(req->path, "/api/marks/delete") == 0) {
        char student_id[32] = {0};
        char course_id[32] = {0};
        json_get_string(req->body, "student_id", student_id, sizeof(student_id));
        json_get_string(req->body, "course_id", course_id, sizeof(course_id));

        if (strlen(student_id) == 0 || strlen(course_id) == 0) {
            http_send_error(req->client_fd, 400, "Missing student_id or course_id");
            return;
        }

        data_access_enter(req->worker_id, "marks_delete");
        int res = data_delete_marks(student_id, course_id);
        data_access_exit(req->worker_id, "marks_delete");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Marks record deleted and synced to mmap file\"}");
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 404, "Marks record not found");
        }
        return;
    }

    /* API Endpoint: POST /api/faculty/create */
    if (strcmp(req->path, "/api/faculty/create") == 0) {
        Faculty f;
        memset(&f, 0, sizeof(f));
        json_get_string(req->body, "id", f.id, sizeof(f.id));
        json_get_string(req->body, "name", f.name, sizeof(f.name));
        json_get_string(req->body, "email", f.email, sizeof(f.email));
        json_get_string(req->body, "department", f.department, sizeof(f.department));
        json_get_string(req->body, "password", f.password, sizeof(f.password));

        if (strlen(f.id) == 0 || strlen(f.name) == 0) {
            http_send_error(req->client_fd, 400, "Missing required faculty fields (id, name)");
            return;
        }

        if (strlen(f.password) == 0) strcpy(f.password, "fac123");

        data_access_enter(req->worker_id, "faculty_insert");
        int res = data_insert_faculty(&f);
        data_access_exit(req->worker_id, "faculty_insert");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Faculty created and synced to mmap file\", \"faculty\": {\"id\": \"%s\", \"name\": \"%s\"}}",
                f.id, f.name);
            http_send_json(req->client_fd, 201, resp);
        } else {
            http_send_error(req->client_fd, 409, "Faculty with this ID already exists or storage full");
        }
        return;
    }

    /* API Endpoint: POST /api/faculty/update */
    if (strcmp(req->path, "/api/faculty/update") == 0) {
        Faculty f;
        memset(&f, 0, sizeof(f));
        json_get_string(req->body, "id", f.id, sizeof(f.id));
        json_get_string(req->body, "name", f.name, sizeof(f.name));
        json_get_string(req->body, "email", f.email, sizeof(f.email));
        json_get_string(req->body, "department", f.department, sizeof(f.department));
        json_get_string(req->body, "password", f.password, sizeof(f.password));

        if (strlen(f.id) == 0) {
            http_send_error(req->client_fd, 400, "Missing faculty id");
            return;
        }

        data_access_enter(req->worker_id, "faculty_update");
        int res = data_update_faculty(&f);
        data_access_exit(req->worker_id, "faculty_update");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Faculty updated and synced to mmap file\", \"faculty\": {\"id\": \"%s\", \"name\": \"%s\"}}",
                f.id, f.name);
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 404, "Faculty not found");
        }
        return;
    }

    /* API Endpoint: POST /api/faculty/delete */
    if (strcmp(req->path, "/api/faculty/delete") == 0) {
        char faculty_id[32] = {0};
        json_get_string(req->body, "id", faculty_id, sizeof(faculty_id));
        if (strlen(faculty_id) == 0) {
            query_get_param(req->query, "id", faculty_id, sizeof(faculty_id));
        }

        if (strlen(faculty_id) == 0) {
            http_send_error(req->client_fd, 400, "Missing faculty id");
            return;
        }

        data_access_enter(req->worker_id, "faculty_delete");
        int res = data_delete_faculty(faculty_id);
        data_access_exit(req->worker_id, "faculty_delete");

        if (res == 0) {
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"success\": true, \"message\": \"Faculty %s deleted and synced to mmap file\"}",
                faculty_id);
            http_send_json(req->client_fd, 200, resp);
        } else {
            http_send_error(req->client_fd, 404, "Faculty not found");
        }
        return;
    }

    /* If not an API route, serve static file from web/ */
    http_serve_file(req->client_fd, req->path);
}

void process_client_request(int worker_id, int client_fd, const char *client_ip, int client_port) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.failed_requests++;
        pthread_mutex_unlock(&g_stats_mutex);
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0';

    HttpRequest req;
    memset(&req, 0, sizeof(req));
    req.client_fd = client_fd;
    req.worker_id = worker_id;
    snprintf(req.client_ip, sizeof(req.client_ip), "%s", client_ip ? client_ip : "unknown");
    req.client_port = client_port;

    if (http_parse_request(buffer, (int)bytes_read, &req) == 0) {
        http_handle_request(&req);
    } else {
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.failed_requests++;
        pthread_mutex_unlock(&g_stats_mutex);
        http_send_error(client_fd, 400, "Malformed HTTP request");
    }
}
