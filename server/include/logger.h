#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include <stddef.h>

#define DEFAULT_SERVER_LOG "server/logs/server.log"
#define DEFAULT_TRANSACTIONS_LOG "server/logs/transactions.log"

extern pthread_mutex_t g_logger_mutex;

int logger_init(const char *server_log_path, const char *tx_log_path);
void logger_server(const char *level, const char *fmt, ...);
void logger_transaction(int worker_id, const char *op, const char *record_id, int success, double duration_ms, const char *path);
void logger_close(void);

int logger_get_recent_server_logs(char *buffer, size_t max_size, int max_lines);
int logger_get_recent_transactions(char *buffer, size_t max_size, int max_lines);

#endif /* LOGGER_H */
