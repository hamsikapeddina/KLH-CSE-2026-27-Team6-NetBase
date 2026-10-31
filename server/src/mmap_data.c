#include "mmap_data.h"
#include "pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

MmapUniversityStore *g_mmap_store = NULL;
static int g_mmap_fd = -1;
static char g_mmap_path[256] = {0};
static size_t g_mmap_size = 0;
static int g_mmap_active = 0;

static void update_sync_timestamp(void) {
    if (!g_mmap_store) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        strftime(g_mmap_store->last_sync_time, sizeof(g_mmap_store->last_sync_time), "%Y-%m-%d %H:%M:%S", t);
    }
}

static void calculate_grade_and_total(float midterm, float endterm, float quiz, float *out_total, char *out_grade) {
    float total = ((midterm + endterm + quiz) / 175.0f) * 100.0f;
    if (total > 100.0f) total = 100.0f;
    if (total < 0.0f) total = 0.0f;
    *out_total = total;

    if (total >= 90.0f) strcpy(out_grade, "A+");
    else if (total >= 80.0f) strcpy(out_grade, "A");
    else if (total >= 70.0f) strcpy(out_grade, "B");
    else if (total >= 60.0f) strcpy(out_grade, "C");
    else if (total >= 50.0f) strcpy(out_grade, "D");
    else strcpy(out_grade, "F");
}

static void seed_default_records(void) {
    if (!g_mmap_store) return;

    g_mmap_store->student_count = 0;
    g_mmap_store->faculty_count = 0;
    g_mmap_store->course_count = 0;
    g_mmap_store->marks_count = 0;
    g_mmap_store->attendance_count = 0;
    g_mmap_store->transaction_count = 0;

    /* 1. Students */
    Student s1 = {"S101", "Aarav Sharma", "aarav.sharma@univ.edu", "Computer Science", 3, "A", "pass101"};
    Student s2 = {"S102", "Priya Patel", "priya.patel@univ.edu", "Computer Science", 3, "A", "pass102"};
    Student s3 = {"S103", "Rohan Verma", "rohan.verma@univ.edu", "Information Tech", 2, "B", "pass103"};
    Student s4 = {"S104", "Ananya Iyer", "ananya.iyer@univ.edu", "Computer Science", 3, "A", "pass104"};
    g_mmap_store->students[g_mmap_store->student_count++] = s1;
    g_mmap_store->students[g_mmap_store->student_count++] = s2;
    g_mmap_store->students[g_mmap_store->student_count++] = s3;
    g_mmap_store->students[g_mmap_store->student_count++] = s4;

    /* 2. Faculty */
    Faculty f1 = {"F201", "Dr. Vikram Rao", "vikram.rao@univ.edu", "Computer Science", "fac201"};
    Faculty f2 = {"F202", "Prof. Neha Gupta", "neha.gupta@univ.edu", "Information Tech", "fac202"};
    Faculty f3 = {"F203", "Dr. Rajesh Kulkarni", "rajesh.k@univ.edu", "Computer Science", "fac203"};
    g_mmap_store->faculty[g_mmap_store->faculty_count++] = f1;
    g_mmap_store->faculty[g_mmap_store->faculty_count++] = f2;
    g_mmap_store->faculty[g_mmap_store->faculty_count++] = f3;

    /* 3. Courses */
    Course c1 = {"CS301", "Operating Systems", 4, "Computer Science", 5, "F201"};
    Course c2 = {"CS302", "Computer Networks", 4, "Computer Science", 5, "F202"};
    Course c3 = {"CS303", "Database Management", 3, "Computer Science", 5, "F203"};
    Course c4 = {"CS304", "System Programming & Linux", 3, "Computer Science", 5, "F201"};
    g_mmap_store->courses[g_mmap_store->course_count++] = c1;
    g_mmap_store->courses[g_mmap_store->course_count++] = c2;
    g_mmap_store->courses[g_mmap_store->course_count++] = c3;
    g_mmap_store->courses[g_mmap_store->course_count++] = c4;

    /* 4. Marks Records (Midterm/50, Endterm/100, Quiz/25) */
    MarkRecord m;
    #define ADD_MARK(sid, cid, mid, end, qz) do { \
        memset(&m, 0, sizeof(m)); \
        strncpy(m.student_id, sid, sizeof(m.student_id) - 1); \
        strncpy(m.course_id, cid, sizeof(m.course_id) - 1); \
        m.midterm = mid; m.endterm = end; m.quiz = qz; \
        calculate_grade_and_total(mid, end, qz, &m.total_pct, m.grade); \
        g_mmap_store->marks[g_mmap_store->marks_count++] = m; \
    } while(0)

    ADD_MARK("S101", "CS301", 42.0f, 88.0f, 22.0f);
    ADD_MARK("S101", "CS302", 39.0f, 81.0f, 20.0f);
    ADD_MARK("S101", "CS303", 46.0f, 92.0f, 24.0f);
    ADD_MARK("S101", "CS304", 40.0f, 84.0f, 21.0f);

    ADD_MARK("S102", "CS301", 47.0f, 94.0f, 25.0f);
    ADD_MARK("S102", "CS302", 44.0f, 89.0f, 23.0f);
    ADD_MARK("S102", "CS303", 45.0f, 90.0f, 23.0f);
    ADD_MARK("S102", "CS304", 46.0f, 91.0f, 24.0f);

    ADD_MARK("S103", "CS302", 32.0f, 68.0f, 17.0f);
    ADD_MARK("S103", "CS303", 35.0f, 72.0f, 18.0f);

    ADD_MARK("S104", "CS301", 41.0f, 85.0f, 22.0f);
    ADD_MARK("S104", "CS303", 43.0f, 87.0f, 22.0f);
    ADD_MARK("S104", "CS304", 44.0f, 88.0f, 23.0f);
    #undef ADD_MARK

    /* 5. Attendance Records */
    AttendanceRecord a;
    #define ADD_ATT(sid, cid, att, tot) do { \
        memset(&a, 0, sizeof(a)); \
        strncpy(a.student_id, sid, sizeof(a.student_id) - 1); \
        strncpy(a.course_id, cid, sizeof(a.course_id) - 1); \
        a.attended_classes = att; a.total_classes = tot; \
        a.percentage = ((float)att / (float)tot) * 100.0f; \
        g_mmap_store->attendance[g_mmap_store->attendance_count++] = a; \
    } while(0)

    ADD_ATT("S101", "CS301", 28, 32);
    ADD_ATT("S101", "CS302", 26, 30);
    ADD_ATT("S101", "CS303", 27, 28);
    ADD_ATT("S101", "CS304", 25, 30);

    ADD_ATT("S102", "CS301", 31, 32);
    ADD_ATT("S102", "CS302", 29, 30);
    ADD_ATT("S102", "CS303", 27, 28);
    ADD_ATT("S102", "CS304", 29, 30);

    ADD_ATT("S103", "CS302", 21, 30);
    ADD_ATT("S103", "CS303", 20, 28);

    ADD_ATT("S104", "CS301", 29, 32);
    ADD_ATT("S104", "CS303", 26, 28);
    ADD_ATT("S104", "CS304", 28, 30);
    #undef ADD_ATT

    update_sync_timestamp();
}

int mmap_data_init(const char *filepath) {
    if (!filepath || strlen(filepath) == 0) {
        filepath = DEFAULT_MMAP_FILE;
    }

    /* Ensure directory exists */
    mkdir("server/data", 0755);
    mkdir("data", 0755);

    strncpy(g_mmap_path, filepath, sizeof(g_mmap_path) - 1);
    g_mmap_size = sizeof(MmapUniversityStore);

    /* Open or create binary data file using POSIX open() */
    g_mmap_fd = open(g_mmap_path, O_RDWR | O_CREAT, 0644);
    if (g_mmap_fd < 0) {
        /* Try fallback path if first path failed */
        g_mmap_fd = open(FALLBACK_MMAP_FILE, O_RDWR | O_CREAT, 0644);
        if (g_mmap_fd >= 0) {
            strncpy(g_mmap_path, FALLBACK_MMAP_FILE, sizeof(g_mmap_path) - 1);
        } else {
            fprintf(stderr, "[MMAP] Failed to open %s: %s\n", filepath, strerror(errno));
            return -1;
        }
    }

    /* Check file size via POSIX fstat() */
    struct stat st;
    if (fstat(g_mmap_fd, &st) < 0) {
        perror("[MMAP] fstat error");
        close(g_mmap_fd);
        g_mmap_fd = -1;
        return -1;
    }

    int need_seed = 0;
    if ((size_t)st.st_size < g_mmap_size) {
        /* Resize file to hold complete MmapUniversityStore structure using ftruncate() */
        if (ftruncate(g_mmap_fd, g_mmap_size) < 0) {
            perror("[MMAP] ftruncate error");
            close(g_mmap_fd);
            g_mmap_fd = -1;
            return -1;
        }
        need_seed = 1;
    }

    /* MAP_SHARED makes modifications visible through the backing file. */
    void *mapped_ptr = mmap(NULL, g_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_mmap_fd, 0);
    if (mapped_ptr == MAP_FAILED) {
        perror("[MMAP] mmap error");
        close(g_mmap_fd);
        g_mmap_fd = -1;
        return -1;
    }

    g_mmap_store = (MmapUniversityStore*)mapped_ptr;

    /* Validate header magic and version */
    if (need_seed || g_mmap_store->magic != MMAP_MAGIC || g_mmap_store->version != MMAP_VERSION) {
        printf("[MMAP] Initializing fresh persistent memory-mapped file at %s (%zu bytes)\n",
               g_mmap_path, g_mmap_size);
        memset(g_mmap_store, 0, g_mmap_size);
        g_mmap_store->magic = MMAP_MAGIC;
        g_mmap_store->version = MMAP_VERSION;
        seed_default_records();
        msync(g_mmap_store, g_mmap_size, MS_SYNC);
    } else {
        printf("[MMAP] Recovered existing persistent memory-mapped data from %s (TxCount: %u, Students: %u)\n",
               g_mmap_path, g_mmap_store->transaction_count, g_mmap_store->student_count);
    }

    g_mmap_active = 1;
    return 0;
}

int mmap_data_sync(void) {
    if (!g_mmap_active || !g_mmap_store) return -1;
    g_mmap_store->transaction_count++;
    update_sync_timestamp();
    /* Flushes dirty memory pages to disk backing store */
    return msync(g_mmap_store, g_mmap_size, MS_ASYNC);
}

void mmap_data_close(void) {
    if (!g_mmap_active || !g_mmap_store) return;
    msync(g_mmap_store, g_mmap_size, MS_SYNC);
    munmap(g_mmap_store, g_mmap_size);
    g_mmap_store = NULL;
    if (g_mmap_fd >= 0) {
        close(g_mmap_fd);
        g_mmap_fd = -1;
    }
    g_mmap_active = 0;
}

void mmap_data_unmap(void) {
    mmap_data_close();
}

int mmap_data_is_active(void) {
    return g_mmap_active;
}

const char* mmap_data_get_path(void) {
    return g_mmap_path;
}

size_t mmap_data_get_size(void) {
    return g_mmap_size;
}

uint32_t mmap_data_get_version(void) {
    return g_mmap_store ? g_mmap_store->version : 0;
}

uint32_t mmap_data_get_tx_count(void) {
    return g_mmap_store ? g_mmap_store->transaction_count : 0;
}

/* CRUD: Student Operations */

int mmap_student_insert(const Student *student) {
    if (!g_mmap_store || !student) return -1;
    if (g_mmap_store->student_count >= MAX_STUDENTS) return -2;

    /* Check duplicate ID */
    for (uint32_t i = 0; i < g_mmap_store->student_count; i++) {
        if (strcmp(g_mmap_store->students[i].id, student->id) == 0) {
            return -3; /* Duplicate ID */
        }
    }

    g_mmap_store->students[g_mmap_store->student_count++] = *student;

    /* Initialize academic records for all active courses in default state */
    char target_sid[16];
    snprintf(target_sid, sizeof(target_sid), "%s", student->id);

    for (uint32_t c = 0; c < g_mmap_store->course_count; c++) {
        char target_cid[16];
        snprintf(target_cid, sizeof(target_cid), "%s", g_mmap_store->courses[c].id);

        /* Check if marks record already exists */
        int mark_exists = 0;
        for (uint32_t m = 0; m < g_mmap_store->marks_count; m++) {
            if (strcmp(g_mmap_store->marks[m].student_id, target_sid) == 0 &&
                strcmp(g_mmap_store->marks[m].course_id, target_cid) == 0) {
                mark_exists = 1;
                break;
            }
        }
        if (!mark_exists && g_mmap_store->marks_count < MAX_MARKS) {
            MarkRecord *mr = &g_mmap_store->marks[g_mmap_store->marks_count++];
            memset(mr, 0, sizeof(MarkRecord));
            strcpy(mr->student_id, target_sid);
            strcpy(mr->course_id, target_cid);
            mr->midterm = 0.0f;
            mr->endterm = 0.0f;
            mr->quiz = 0.0f;
            mr->total_pct = 0.0f;
            strcpy(mr->grade, "-");
        }

        /* Check if attendance record already exists */
        int att_exists = 0;
        for (uint32_t a = 0; a < g_mmap_store->attendance_count; a++) {
            if (strcmp(g_mmap_store->attendance[a].student_id, target_sid) == 0 &&
                strcmp(g_mmap_store->attendance[a].course_id, target_cid) == 0) {
                att_exists = 1;
                break;
            }
        }
        if (!att_exists && g_mmap_store->attendance_count < MAX_ATTENDANCE) {
            AttendanceRecord *ar = &g_mmap_store->attendance[g_mmap_store->attendance_count++];
            memset(ar, 0, sizeof(AttendanceRecord));
            strcpy(ar->student_id, target_sid);
            strcpy(ar->course_id, target_cid);
            ar->attended_classes = 0;
            ar->total_classes = 0;
            ar->percentage = 0.0f;
        }
    }

    mmap_data_sync();
    return 0;
}

const Student* mmap_student_find(const char *id) {
    if (!g_mmap_store || !id) return NULL;
    for (uint32_t i = 0; i < g_mmap_store->student_count; i++) {
        if (strcmp(g_mmap_store->students[i].id, id) == 0) {
            return &g_mmap_store->students[i];
        }
    }
    return NULL;
}

int mmap_student_update(const Student *student) {
    if (!g_mmap_store || !student) return -1;
    for (uint32_t i = 0; i < g_mmap_store->student_count; i++) {
        if (strcmp(g_mmap_store->students[i].id, student->id) == 0) {
            if (strlen(student->name) > 0)
                snprintf(g_mmap_store->students[i].name, sizeof(g_mmap_store->students[i].name), "%s", student->name);
            if (strlen(student->email) > 0)
                snprintf(g_mmap_store->students[i].email, sizeof(g_mmap_store->students[i].email), "%s", student->email);
            if (strlen(student->department) > 0)
                snprintf(g_mmap_store->students[i].department, sizeof(g_mmap_store->students[i].department), "%s", student->department);
            if (student->year > 0)
                g_mmap_store->students[i].year = student->year;
            if (strlen(student->section) > 0)
                snprintf(g_mmap_store->students[i].section, sizeof(g_mmap_store->students[i].section), "%s", student->section);
            if (strlen(student->password) > 0)
                snprintf(g_mmap_store->students[i].password, sizeof(g_mmap_store->students[i].password), "%s", student->password);

            mmap_data_sync();
            return 0;
        }
    }
    return -2; /* Not found */
}

int mmap_student_delete(const char *id) {
    if (!g_mmap_store || !id) return -1;
    for (uint32_t i = 0; i < g_mmap_store->student_count; i++) {
        if (strcmp(g_mmap_store->students[i].id, id) == 0) {
            /* Shift subsequent items left */
            for (uint32_t j = i; j < g_mmap_store->student_count - 1; j++) {
                g_mmap_store->students[j] = g_mmap_store->students[j + 1];
            }
            g_mmap_store->student_count--;

            /* Remove all marks for this student */
            for (uint32_t m = 0; m < g_mmap_store->marks_count; ) {
                if (strcmp(g_mmap_store->marks[m].student_id, id) == 0) {
                    for (uint32_t k = m; k < g_mmap_store->marks_count - 1; k++) {
                        g_mmap_store->marks[k] = g_mmap_store->marks[k + 1];
                    }
                    g_mmap_store->marks_count--;
                } else {
                    m++;
                }
            }

            /* Remove all attendance for this student */
            for (uint32_t a = 0; a < g_mmap_store->attendance_count; ) {
                if (strcmp(g_mmap_store->attendance[a].student_id, id) == 0) {
                    for (uint32_t k = a; k < g_mmap_store->attendance_count - 1; k++) {
                        g_mmap_store->attendance[k] = g_mmap_store->attendance[k + 1];
                    }
                    g_mmap_store->attendance_count--;
                } else {
                    a++;
                }
            }

            mmap_data_sync();
            return 0;
        }
    }
    return -2; /* Not found */
}

/* CRUD: Faculty Operations */

int mmap_faculty_insert(const Faculty *faculty) {
    if (!g_mmap_store || !faculty) return -1;
    if (g_mmap_store->faculty_count >= MAX_FACULTY) return -2;

    /* Check duplicate ID */
    for (uint32_t i = 0; i < g_mmap_store->faculty_count; i++) {
        if (strcmp(g_mmap_store->faculty[i].id, faculty->id) == 0) {
            return -3; /* Duplicate ID */
        }
    }

    g_mmap_store->faculty[g_mmap_store->faculty_count++] = *faculty;
    mmap_data_sync();
    return 0;
}

const Faculty* mmap_faculty_find(const char *id) {
    if (!g_mmap_store || !id) return NULL;
    for (uint32_t i = 0; i < g_mmap_store->faculty_count; i++) {
        if (strcmp(g_mmap_store->faculty[i].id, id) == 0) {
            return &g_mmap_store->faculty[i];
        }
    }
    return NULL;
}

int mmap_faculty_update(const Faculty *faculty) {
    if (!g_mmap_store || !faculty) return -1;
    for (uint32_t i = 0; i < g_mmap_store->faculty_count; i++) {
        if (strcmp(g_mmap_store->faculty[i].id, faculty->id) == 0) {
            if (strlen(faculty->name) > 0)
                snprintf(g_mmap_store->faculty[i].name, sizeof(g_mmap_store->faculty[i].name), "%s", faculty->name);
            if (strlen(faculty->email) > 0)
                snprintf(g_mmap_store->faculty[i].email, sizeof(g_mmap_store->faculty[i].email), "%s", faculty->email);
            if (strlen(faculty->department) > 0)
                snprintf(g_mmap_store->faculty[i].department, sizeof(g_mmap_store->faculty[i].department), "%s", faculty->department);
            if (strlen(faculty->password) > 0)
                snprintf(g_mmap_store->faculty[i].password, sizeof(g_mmap_store->faculty[i].password), "%s", faculty->password);

            mmap_data_sync();
            return 0;
        }
    }
    return -2; /* Not found */
}

int mmap_faculty_delete(const char *id) {
    if (!g_mmap_store || !id) return -1;
    for (uint32_t i = 0; i < g_mmap_store->faculty_count; i++) {
        if (strcmp(g_mmap_store->faculty[i].id, id) == 0) {
            for (uint32_t j = i; j < g_mmap_store->faculty_count - 1; j++) {
                g_mmap_store->faculty[j] = g_mmap_store->faculty[j + 1];
            }
            g_mmap_store->faculty_count--;
            mmap_data_sync();
            return 0;
        }
    }
    return -2; /* Not found */
}

/* CRUD: Marks Operations */

int mmap_marks_update(const char *student_id, const char *course_id, float mid, float end, float qz, MarkRecord *out) {
    if (!g_mmap_store || !student_id || !course_id) return -1;

    for (uint32_t i = 0; i < g_mmap_store->marks_count; i++) {
        if (strcmp(g_mmap_store->marks[i].student_id, student_id) == 0 &&
            strcmp(g_mmap_store->marks[i].course_id, course_id) == 0) {
            g_mmap_store->marks[i].midterm = mid;
            g_mmap_store->marks[i].endterm = end;
            g_mmap_store->marks[i].quiz = qz;
            calculate_grade_and_total(mid, end, qz, &g_mmap_store->marks[i].total_pct, g_mmap_store->marks[i].grade);
            if (out) *out = g_mmap_store->marks[i];
            mmap_data_sync();
            return 0;
        }
    }

    /* Insert new marks record if not found */
    if (g_mmap_store->marks_count >= MAX_MARKS) return -2;
    MarkRecord *m = &g_mmap_store->marks[g_mmap_store->marks_count++];
    memset(m, 0, sizeof(*m));
    strncpy(m->student_id, student_id, sizeof(m->student_id) - 1);
    strncpy(m->course_id, course_id, sizeof(m->course_id) - 1);
    m->midterm = mid;
    m->endterm = end;
    m->quiz = qz;
    calculate_grade_and_total(mid, end, qz, &m->total_pct, m->grade);
    if (out) *out = *m;
    mmap_data_sync();
    return 0;
}

int mmap_marks_delete(const char *student_id, const char *course_id) {
    if (!g_mmap_store || !student_id || !course_id) return -1;
    for (uint32_t i = 0; i < g_mmap_store->marks_count; i++) {
        if (strcmp(g_mmap_store->marks[i].student_id, student_id) == 0 &&
            strcmp(g_mmap_store->marks[i].course_id, course_id) == 0) {
            for (uint32_t j = i; j < g_mmap_store->marks_count - 1; j++) {
                g_mmap_store->marks[j] = g_mmap_store->marks[j + 1];
            }
            g_mmap_store->marks_count--;
            mmap_data_sync();
            return 0;
        }
    }
    return -2; /* Not found */
}

/* CRUD: Attendance Operations */

int mmap_attendance_update(const char *student_id, const char *course_id, int attended, int total, AttendanceRecord *out) {
    if (!g_mmap_store || !student_id || !course_id) return -1;

    for (uint32_t i = 0; i < g_mmap_store->attendance_count; i++) {
        if (strcmp(g_mmap_store->attendance[i].student_id, student_id) == 0 &&
            strcmp(g_mmap_store->attendance[i].course_id, course_id) == 0) {
            g_mmap_store->attendance[i].attended_classes = attended;
            g_mmap_store->attendance[i].total_classes = (total > 0) ? total : 1;
            g_mmap_store->attendance[i].percentage = ((float)attended / (float)g_mmap_store->attendance[i].total_classes) * 100.0f;
            if (out) *out = g_mmap_store->attendance[i];
            mmap_data_sync();
            return 0;
        }
    }

    if (g_mmap_store->attendance_count >= MAX_ATTENDANCE) return -2;
    AttendanceRecord *a = &g_mmap_store->attendance[g_mmap_store->attendance_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->student_id, student_id, sizeof(a->student_id) - 1);
    strncpy(a->course_id, course_id, sizeof(a->course_id) - 1);
    a->attended_classes = attended;
    a->total_classes = (total > 0) ? total : 1;
    a->percentage = ((float)attended / (float)a->total_classes) * 100.0f;
    if (out) *out = *a;
    mmap_data_sync();
    return 0;
}
