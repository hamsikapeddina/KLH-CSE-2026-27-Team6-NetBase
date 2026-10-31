#ifndef PROC_MONITOR_H
#define PROC_MONITOR_H

#include <sys/types.h>

/* Real Linux /proc Metrics */
typedef struct {
    pid_t pid;
    char comm[64];
    char state_char;
    char state_name[64];
    pid_t ppid;
    unsigned long utime_ticks;
    unsigned long stime_ticks;
    double cpu_time_seconds;
    long num_threads;
    unsigned long vsize_bytes;
    unsigned long vmrss_bytes;
    unsigned long vsize_kb;
    unsigned long vmrss_kb;
    int open_fds;
    long page_size;
    long clock_ticks_per_sec;
    unsigned long uptime_seconds;
} ProcMetrics;

int proc_get_metrics(ProcMetrics *metrics);

#endif /* PROC_MONITOR_H */
