#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

ServerConfig g_config = {
    .port = 3000,
    .log_enabled = 1,
    .data_access_slots = 2,
    .request_queue_capacity = 32,
    .response_queue_capacity = 32,
    .max_workers = 4,
    .server_name = "University-ERP-POSIX-C",
    .web_root = "web",
    .data_file = "server/data/university.dat",
    .server_log = "server/logs/server.log",
    .transactions_log = "server/logs/transactions.log",
    .reload_count = 0,
    .last_reload_time = "Initial Boot",
    .config_file = "server/config/server.conf"
};

static void trim(char *s) {
    char *p = s;
    int l = strlen(p);
    while (l > 0 && isspace((unsigned char)p[l - 1])) p[--l] = 0;
    while (*p && isspace((unsigned char)*p)) ++p, --l;
    memmove(s, p, l + 1);
}

int config_init(const char *filepath) {
    if (filepath && strlen(filepath) > 0) {
        strncpy(g_config.config_file, filepath, sizeof(g_config.config_file) - 1);
    }
    return config_reload();
}

int config_reload(void) {
    FILE *fp = fopen(g_config.config_file, "r");
    if (!fp) {
        logger_server("WARN", "[CONFIG] Could not open config file '%s', keeping current settings", g_config.config_file);
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "port") == 0) {
            int p = atoi(val);
            if (p > 0 && p <= 65535) g_config.port = p;
        } else if (strcmp(key, "log_enabled") == 0) {
            g_config.log_enabled = atoi(val);
        } else if (strcmp(key, "data_access_slots") == 0) {
            int s = atoi(val);
            if (s > 0) g_config.data_access_slots = s;
        } else if (strcmp(key, "request_queue_capacity") == 0) {
            int q = atoi(val);
            if (q > 0) g_config.request_queue_capacity = q;
        } else if (strcmp(key, "response_queue_capacity") == 0) {
            int q = atoi(val);
            if (q > 0) g_config.response_queue_capacity = q;
        } else if (strcmp(key, "max_workers") == 0) {
            int w = atoi(val);
            if (w > 0) g_config.max_workers = w;
        } else if (strcmp(key, "server_name") == 0) {
            strncpy(g_config.server_name, val, sizeof(g_config.server_name) - 1);
        } else if (strcmp(key, "web_root") == 0) {
            strncpy(g_config.web_root, val, sizeof(g_config.web_root) - 1);
        } else if (strcmp(key, "data_file") == 0) {
            strncpy(g_config.data_file, val, sizeof(g_config.data_file) - 1);
        } else if (strcmp(key, "server_log") == 0) {
            strncpy(g_config.server_log, val, sizeof(g_config.server_log) - 1);
        } else if (strcmp(key, "transactions_log") == 0) {
            strncpy(g_config.transactions_log, val, sizeof(g_config.transactions_log) - 1);
        }
    }
    fclose(fp);

    g_config.reload_count++;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(g_config.last_reload_time, sizeof(g_config.last_reload_time), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    logger_server("INFO", "[CONFIG-RELOAD] SIGHUP dynamic config reload #%d successful from %s",
                  g_config.reload_count, g_config.config_file);
    return 0;
}
