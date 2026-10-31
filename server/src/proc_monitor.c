#include "proc_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <time.h>

static const char *get_state_description(char state) {
    switch (state) {
        case 'R': return "Running (R)";
        case 'S': return "Sleeping (Interruptible) (S)";
        case 'D': return "Disk Sleep (Uninterruptible) (D)";
        case 'Z': return "Zombie (Z)";
        case 'T': return "Stopped (T)";
        case 't': return "Tracing Stop (t)";
        case 'W': return "Paging (W)";
        case 'X': return "Dead (X)";
        default:  return "Active (Linux Kernel)";
    }
}

int proc_get_metrics(ProcMetrics *m) {
    if (!m) return -1;
    memset(m, 0, sizeof(ProcMetrics));

    m->pid = getpid();
    m->ppid = getppid();
    m->page_size = sysconf(_SC_PAGESIZE);
    m->clock_ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (m->clock_ticks_per_sec <= 0) m->clock_ticks_per_sec = 100;
    if (m->page_size <= 0) m->page_size = 4096;

    /* 1. Parse /proc/self/stat */
    FILE *fp = fopen("/proc/self/stat", "r");
    if (fp) {
        char buf[1024];
        if (fgets(buf, sizeof(buf), fp)) {
            /* comm is inside parentheses e.g. "1234 (server) S ..." */
            char *open_paren = strchr(buf, '(');
            char *close_paren = strrchr(buf, ')');
            if (open_paren && close_paren && close_paren > open_paren) {
                size_t comm_len = close_paren - open_paren - 1;
                if (comm_len >= sizeof(m->comm)) comm_len = sizeof(m->comm) - 1;
                strncpy(m->comm, open_paren + 1, comm_len);
                m->comm[comm_len] = '\0';

                /* Parse remaining fields after close_paren */
                char state;
                int ppid, pgrp, session, tty_nr, tpgid;
                unsigned int flags;
                unsigned long minflt, cminflt, majflt, cmajflt;
                unsigned long utime, stime;
                long cutime, cstime, priority, nice, num_threads, itrealvalue;
                unsigned long long starttime;
                unsigned long vsize;
                long rss;

                int parsed = sscanf(close_paren + 2,
                    "%c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld",
                    &state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
                    &flags, &minflt, &cminflt, &majflt, &cmajflt,
                    &utime, &stime, &cutime, &cstime, &priority, &nice,
                    &num_threads, &itrealvalue, &starttime,
                    &vsize, &rss);

                if (parsed >= 1) {
                    m->state_char = state;
                    strncpy(m->state_name, get_state_description(state), sizeof(m->state_name) - 1);
                }
                if (parsed >= 2) m->ppid = ppid;
                if (parsed >= 14) {
                    m->utime_ticks = utime;
                    m->stime_ticks = stime;
                    m->cpu_time_seconds = (double)(utime + stime) / (double)m->clock_ticks_per_sec;
                }
                if (parsed >= 18) m->num_threads = num_threads;
                if (parsed >= 21) {
                    m->vsize_bytes = vsize;
                    m->vsize_kb = vsize / 1024;
                }
                if (parsed >= 22) {
                    m->vmrss_bytes = (unsigned long)rss * m->page_size;
                    m->vmrss_kb = m->vmrss_bytes / 1024;
                }
            }
        }
        fclose(fp);
    } else {
        strncpy(m->comm, "server", sizeof(m->comm) - 1);
        m->state_char = 'R';
        strncpy(m->state_name, "Running", sizeof(m->state_name) - 1);
    }

    /* 2. Count Open File Descriptors via /proc/self/fd */
    DIR *dir = opendir("/proc/self/fd");
    if (dir) {
        int fd_count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                fd_count++;
            }
        }
        closedir(dir);
        m->open_fds = fd_count;
    } else {
        m->open_fds = 4; /* fallback estimate: stdin, stdout, stderr, listen_socket */
    }

    /* 3. System Uptime via /proc/uptime */
    FILE *ufp = fopen("/proc/uptime", "r");
    if (ufp) {
        double up = 0;
        if (fscanf(ufp, "%lf", &up) == 1) {
            m->uptime_seconds = (unsigned long)up;
        }
        fclose(ufp);
    }

    return 0;
}
