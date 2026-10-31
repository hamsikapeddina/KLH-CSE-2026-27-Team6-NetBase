#ifndef MMAP_DATA_H
#define MMAP_DATA_H

#include "data.h"
#include <stdint.h>
#include <sys/types.h>

#define MMAP_MAGIC 0x554E4956  /* "UNIV" in ASCII */
#define MMAP_VERSION 1
#define DEFAULT_MMAP_FILE "server/data/university.dat"
#define FALLBACK_MMAP_FILE "data/university.dat"

/* Memory-Mapped University Database Layout */
typedef struct {
    uint32_t magic;               /* Magic number for validation (0x554E4956) */
    uint32_t version;             /* Store format version */
    uint32_t student_count;       /* Number of active student records */
    uint32_t faculty_count;       /* Number of active faculty records */
    uint32_t course_count;        /* Number of active course records */
    uint32_t marks_count;         /* Number of active marks records */
    uint32_t attendance_count;    /* Number of active attendance records */
    uint32_t transaction_count;   /* Monotonic transaction count */
    char last_sync_time[32];      /* Timestamp of last msync() */
    uint32_t reserved[8];         /* Reserved alignment padding */

    /* Fixed-size structured tables */
    Student students[MAX_STUDENTS];
    Faculty faculty[MAX_FACULTY];
    Course courses[MAX_COURSES];
    MarkRecord marks[MAX_MARKS];
    AttendanceRecord attendance[MAX_ATTENDANCE];
} MmapUniversityStore;

extern MmapUniversityStore *g_mmap_store;

/* Lifecycle */
int mmap_data_init(const char *filepath);
int mmap_data_sync(void);
void mmap_data_close(void);
void mmap_data_unmap(void);

/* Diagnostic Queries */
int mmap_data_is_active(void);
const char* mmap_data_get_path(void);
size_t mmap_data_get_size(void);
uint32_t mmap_data_get_version(void);
uint32_t mmap_data_get_tx_count(void);

/* CRUD Operations for Students */
int mmap_student_insert(const Student *student);
const Student* mmap_student_find(const char *id);
int mmap_student_update(const Student *student);
int mmap_student_delete(const char *id);

/* CRUD Operations for Faculty */
int mmap_faculty_insert(const Faculty *faculty);
const Faculty* mmap_faculty_find(const char *id);
int mmap_faculty_update(const Faculty *faculty);
int mmap_faculty_delete(const char *id);

/* CRUD Operations for Marks */
int mmap_marks_update(const char *student_id, const char *course_id, float mid, float end, float qz, MarkRecord *out);
int mmap_marks_delete(const char *student_id, const char *course_id);

/* CRUD Operations for Attendance */
int mmap_attendance_update(const char *student_id, const char *course_id, int attended, int total, AttendanceRecord *out);

#endif /* MMAP_DATA_H */
