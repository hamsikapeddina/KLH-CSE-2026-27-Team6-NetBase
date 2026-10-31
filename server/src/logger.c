#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

pthread_mutex_t g_logger_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_server_log_fp = NULL;
static FILE *g_tx_log_fp = NULL;
static char g_server_log_path[256] = DEFAULT_SERVER_LOG;
static char g_tx_log_path[256] = DEFAULT_TRANSACTIONS_LOG;

static void get_timestamp(char *buf, size_t max_len) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(buf, max_len, "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(buf, max_len, "unknown-time");
    }
}

int logger_init(const char *server_log_path, const char *tx_log_path) {
    pthread_mutex_lock(&g_logger_mutex);

    if (server_log_path && strlen(server_log_path) > 0) {
        strncpy(g_server_log_path, server_log_path, sizeof(g_server_log_path) - 1);
    }
    if (tx_log_path && strlen(tx_log_path) > 0) {
        strncpy(g_tx_log_path, tx_log_path, sizeof(g_tx_log_path) - 1);
    }

    if (g_server_log_fp) {
        fclose(g_server_log_fp);
        g_server_log_fp = NULL;
    }
    if (g_tx_log_fp) {
        fclose(g_tx_log_fp);
        g_tx_log_fp = NULL;
    }

    g_server_log_fp = fopen(g_server_log_path, "a");
    if (!g_server_log_fp) {
        perror("[LOGGER] Warning: Could not open server.log for append");
    }

    g_tx_log_fp = fopen(g_tx_log_path, "a");
    if (!g_tx_log_fp) {
        perror("[LOGGER] Warning: Could not open transactions.log for append");
    }

    pthread_mutex_unlock(&g_logger_mutex);
    return (g_server_log_fp && g_tx_log_fp) ? 0 : -1;
}

void logger_server(const char *level, const char *fmt, ...) {
    char time_str[64];
    get_timestamp(time_str, sizeof(time_str));

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    pthread_mutex_lock(&g_logger_mutex);

    /* Output to stdout */
    printf("[%s] [%-5s] %s\n", time_str, level, message);
    fflush(stdout);

    /* Output to server.log */
    if (g_server_log_fp) {
        fprintf(g_server_log_fp, "[%s] [%-5s] %s\n", time_str, level, message);
        fflush(g_server_log_fp);
    }

    pthread_mutex_unlock(&g_logger_mutex);
}

/* Backward compatibility wrapper for server_log in server.h */
void server_log(const char *level, const char *fmt, ...) {
    char time_str[64];
    get_timestamp(time_str, sizeof(time_str));

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    pthread_mutex_lock(&g_logger_mutex);

    printf("[%s] [%-5s] %s\n", time_str, level, message);
    fflush(stdout);

    if (g_server_log_fp) {
        fprintf(g_server_log_fp, "[%s] [%-5s] %s\n", time_str, level, message);
        fflush(g_server_log_fp);
    }

    pthread_mutex_unlock(&g_logger_mutex);
}

void logger_transaction(int worker_id, const char *op, const char *record_id, int success, double duration_ms, const char *path) {
    char time_str[64];
    get_timestamp(time_str, sizeof(time_str));

    pthread_mutex_lock(&g_logger_mutex);

    if (g_tx_log_fp) {
        fprintf(g_tx_log_fp, "[%s] [Worker %d] OP=%s ID=%s STATUS=%s LATENCY=%.2fms PATH=%s\n",
                time_str, worker_id,
                op ? op : "UNKNOWN",
                record_id ? record_id : "N/A",
                success ? "SUCCESS" : "FAILED",
                duration_ms,
                path ? path : "/");
        fflush(g_tx_log_fp);
    }

    pthread_mutex_unlock(&g_logger_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&g_logger_mutex);
    if (g_server_log_fp) {
        fclose(g_server_log_fp);
        g_server_log_fp = NULL;
    }
    if (g_tx_log_fp) {
        fclose(g_tx_log_fp);
        g_tx_log_fp = NULL;
    }
    pthread_mutex_unlock(&g_logger_mutex);
}

/* Helper to read the last N lines of a file into a JSON array string */
static int read_last_lines_as_json(const char *filepath, char *buffer, size_t max_size, int max_lines) {
    if (!filepath || !buffer || max_size < 128) return -1;

    pthread_mutex_lock(&g_logger_mutex);
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        snprintf(buffer, max_size, "[]");
        pthread_mutex_unlock(&g_logger_mutex);
        return 0;
    }

    /* Store up to 100 recent lines in ring */
    char lines[100][512];
    int line_count = 0;
    char temp[512];

    while (fgets(temp, sizeof(temp), fp)) {
        size_t len = strlen(temp);
        if (len > 0 && temp[len - 1] == '\n') temp[len - 1] = '\0';
        if (len > 1 && temp[len - 2] == '\r') temp[len - 2] = '\0';
        if (strlen(temp) == 0) continue;

        snprintf(lines[line_count % 100], sizeof(lines[0]), "%s", temp);
        line_count++;
    }
    fclose(fp);
    pthread_mutex_unlock(&g_logger_mutex);

    int total_to_return = line_count < max_lines ? line_count : max_lines;
    if (total_to_return > 100) total_to_return = 100;
    int start_index = (line_count > total_to_return) ? (line_count - total_to_return) : 0;

    size_t offset = 0;
    offset += snprintf(buffer + offset, max_size - offset, "[\n");

    for (int i = 0; i < total_to_return; i++) {
        int idx = (start_index + i) % 100;
        /* Escape quotes and backslashes in line */
        char escaped[1024] = {0};
        size_t e = 0;
        for (size_t c = 0; c < strlen(lines[idx]) && e < sizeof(escaped) - 2; c++) {
            if (lines[idx][c] == '\"' || lines[idx][c] == '\\') {
                escaped[e++] = '\\';
            }
            escaped[e++] = lines[idx][c];
        }
        escaped[e] = '\0';

        offset += snprintf(buffer + offset, max_size - offset,
                           "  \"%s\"%s\n",
                           escaped,
                           (i == total_to_return - 1) ? "" : ",");
        if (offset >= max_size - 100) break;
    }

    snprintf(buffer + offset, max_size - offset, "]");
    return 0;
}

int logger_get_recent_server_logs(char *buffer, size_t max_size, int max_lines) {
    return read_last_lines_as_json(g_server_log_path, buffer, max_size, max_lines);
}

int logger_get_recent_transactions(char *buffer, size_t max_size, int max_lines) {
    return read_last_lines_as_json(g_tx_log_path, buffer, max_size, max_lines);
}
