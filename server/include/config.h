#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int port;
    int log_enabled;
    int data_access_slots;
    int request_queue_capacity;
    int response_queue_capacity;
    int max_workers;
    char server_name[64];
    char web_root[64];
    char data_file[128];
    char server_log[128];
    char transactions_log[128];
    int reload_count;
    char last_reload_time[64];
    char config_file[128];
} ServerConfig;

extern ServerConfig g_config;

int config_init(const char *filepath);
int config_reload(void);

#endif /* CONFIG_H */
