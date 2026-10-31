#include "data.h"
#include "pool.h"
#include "mmap_data.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define g_students (g_mmap_store->students)
#define g_faculty (g_mmap_store->faculty)
#define g_courses (g_mmap_store->courses)
#define g_marks (g_mmap_store->marks)
#define g_attendance (g_mmap_store->attendance)
#define g_student_count (g_mmap_store->student_count)
#define g_faculty_count (g_mmap_store->faculty_count)
#define g_course_count (g_mmap_store->course_count)
#define g_marks_count (g_mmap_store->marks_count)
#define g_attendance_count (g_mmap_store->attendance_count)

static void calculate_grade_and_total(float midterm, float endterm, float quiz, float *out_total, char *out_grade) {
    /* Maximum possible marks: Midterm (50) + Endterm (100) + Quiz (25) = 175 */
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

/*
 * Normalize legacy mmap records so each student/course pair has exactly one
 * marks record and one attendance record. If older files contain duplicates,
 * retain the last record (the most recently appended value).
 * This runs during single-threaded startup before worker threads begin.
 */
static int normalize_academic_records(void) {
    if (!g_mmap_store) return -1;

    int changed = 0;
    MarkRecord unique_marks[MAX_MARKS];
    uint32_t unique_marks_count = 0;
    if (g_marks_count > MAX_MARKS) return -1;

    for (uint32_t i = 0; i < g_marks_count; ++i) {
        int has_later = 0;
        for (uint32_t j = i + 1; j < g_marks_count; ++j) {
            if (strcmp(g_marks[i].student_id, g_marks[j].student_id) == 0 &&
                strcmp(g_marks[i].course_id, g_marks[j].course_id) == 0) {
                has_later = 1;
                break;
            }
        }
        if (!has_later) unique_marks[unique_marks_count++] = g_marks[i];
        else changed = 1;
    }

    AttendanceRecord unique_attendance[MAX_ATTENDANCE];
    uint32_t unique_attendance_count = 0;
    if (g_attendance_count > MAX_ATTENDANCE) return -1;

    for (uint32_t i = 0; i < g_attendance_count; ++i) {
        int has_later = 0;
        for (uint32_t j = i + 1; j < g_attendance_count; ++j) {
            if (strcmp(g_attendance[i].student_id, g_attendance[j].student_id) == 0 &&
                strcmp(g_attendance[i].course_id, g_attendance[j].course_id) == 0) {
                has_later = 1;
                break;
            }
        }
        if (!has_later) unique_attendance[unique_attendance_count++] = g_attendance[i];
        else changed = 1;
    }

    if (changed) {
        memcpy(g_marks, unique_marks, unique_marks_count * sizeof(MarkRecord));
        g_marks_count = unique_marks_count;
        memcpy(g_attendance, unique_attendance,
               unique_attendance_count * sizeof(AttendanceRecord));
        g_attendance_count = unique_attendance_count;
        mmap_data_sync();
        printf("[DATA] Duplicate student/course records normalized; latest values retained.\\n");
    }
    return 0;
}

int data_init(void) {
    if (mmap_data_init(DEFAULT_MMAP_FILE) != 0) return -1;
    return normalize_academic_records();
}

int data_get_student_count(void) { return g_student_count; }
int data_get_faculty_count(void) { return g_faculty_count; }
int data_get_course_count(void) { return g_course_count; }
int data_get_marks_count(void) { return g_marks_count; }
int data_get_attendance_count(void) { return g_attendance_count; }

const Student* data_get_students(void) { return g_students; }
const Faculty* data_get_faculty(void) { return g_faculty; }
const Course* data_get_courses(void) { return g_courses; }
const MarkRecord* data_get_marks(void) { return g_marks; }
const AttendanceRecord* data_get_attendance(void) { return g_attendance; }

const Student* data_find_student_by_id(const char *id) {
    if (!id) return NULL;
    for (uint32_t i = 0; i < g_student_count; i++) {
        if (strcmp(g_students[i].id, id) == 0) return &g_students[i];
    }
    return NULL;
}

const Faculty* data_find_faculty_by_id(const char *id) {
    if (!id) return NULL;
    for (uint32_t i = 0; i < g_faculty_count; i++) {
        if (strcmp(g_faculty[i].id, id) == 0) return &g_faculty[i];
    }
    return NULL;
}

const Course* data_find_course_by_id(const char *id) {
    if (!id) return NULL;
    for (uint32_t i = 0; i < g_course_count; i++) {
        if (strcmp(g_courses[i].id, id) == 0) return &g_courses[i];
    }
    return NULL;
}

int data_authenticate(const char *role, const char *id, const char *password, void **out_user) {
    if (!role || !id || !password) return -1;

    pthread_mutex_lock(&g_data_mutex);

    if (strcmp(role, "student") == 0) {
        for (uint32_t i = 0; i < g_student_count; i++) {
            if (strcmp(g_students[i].id, id) == 0 && strcmp(g_students[i].password, password) == 0) {
                if (out_user) *out_user = &g_students[i];
                pthread_mutex_unlock(&g_data_mutex);
                return 0;
            }
        }
    } else if (strcmp(role, "faculty") == 0) {
        for (uint32_t i = 0; i < g_faculty_count; i++) {
            if (strcmp(g_faculty[i].id, id) == 0 && strcmp(g_faculty[i].password, password) == 0) {
                if (out_user) *out_user = &g_faculty[i];
                pthread_mutex_unlock(&g_data_mutex);
                return 0;
            }
        }
    }

    pthread_mutex_unlock(&g_data_mutex);
    return -1;
}

int data_update_marks(const char *student_id, const char *course_id, float midterm, float endterm, float quiz, MarkRecord *out_record) {
    if (!student_id || !course_id) return -1;

    pthread_mutex_lock(&g_data_mutex);

    for (uint32_t i = 0; i < g_marks_count; i++) {
        if (strcmp(g_marks[i].student_id, student_id) == 0 && strcmp(g_marks[i].course_id, course_id) == 0) {
            g_marks[i].midterm = midterm;
            g_marks[i].endterm = endterm;
            g_marks[i].quiz = quiz;
            calculate_grade_and_total(midterm, endterm, quiz, &g_marks[i].total_pct, g_marks[i].grade);
            if (out_record) *out_record = g_marks[i];
            mmap_data_sync();
            pthread_mutex_unlock(&g_data_mutex);
            return 0;
        }
    }

    /* If not found, create new record */
    if (g_marks_count < MAX_MARKS) {
        MarkRecord *m = &g_marks[g_marks_count++];
        strncpy(m->student_id, student_id, sizeof(m->student_id) - 1);
        strncpy(m->course_id, course_id, sizeof(m->course_id) - 1);
        m->midterm = midterm;
        m->endterm = endterm;
        m->quiz = quiz;
        calculate_grade_and_total(midterm, endterm, quiz, &m->total_pct, m->grade);
        if (out_record) *out_record = *m;
        mmap_data_sync();
        pthread_mutex_unlock(&g_data_mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_data_mutex);
    return -1;
}

int data_update_attendance(const char *student_id, const char *course_id, int attended, int total, AttendanceRecord *out_record) {
    if (!student_id || !course_id) return -1;

    pthread_mutex_lock(&g_data_mutex);

    for (uint32_t i = 0; i < g_attendance_count; i++) {
        if (strcmp(g_attendance[i].student_id, student_id) == 0 && strcmp(g_attendance[i].course_id, course_id) == 0) {
            g_attendance[i].attended_classes = attended;
            g_attendance[i].total_classes = (total > 0) ? total : 1;
            g_attendance[i].percentage = ((float)attended / (float)g_attendance[i].total_classes) * 100.0f;
            if (out_record) *out_record = g_attendance[i];
            mmap_data_sync();
            pthread_mutex_unlock(&g_data_mutex);
            return 0;
        }
    }

    /* If not found, create new record */
    if (g_attendance_count < MAX_ATTENDANCE) {
        AttendanceRecord *a = &g_attendance[g_attendance_count++];
        strncpy(a->student_id, student_id, sizeof(a->student_id) - 1);
        strncpy(a->course_id, course_id, sizeof(a->course_id) - 1);
        a->attended_classes = attended;
        a->total_classes = (total > 0) ? total : 1;
        a->percentage = ((float)attended / (float)a->total_classes) * 100.0f;
        if (out_record) *out_record = *a;
        mmap_data_sync();
        pthread_mutex_unlock(&g_data_mutex);
        return 0;
    }

    pthread_mutex_unlock(&g_data_mutex);
    return -1;
}

int data_insert_student(const Student *s) {
    if (!s) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_student_insert(s);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

int data_update_student(const Student *s) {
    if (!s) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_student_update(s);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

int data_delete_student(const char *id) {
    if (!id) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_student_delete(id);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

int data_insert_faculty(const Faculty *f) {
    if (!f) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_faculty_insert(f);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

int data_update_faculty(const Faculty *f) {
    if (!f) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_faculty_update(f);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

int data_delete_faculty(const char *id) {
    if (!id) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_faculty_delete(id);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

int data_delete_marks(const char *student_id, const char *course_id) {
    if (!student_id || !course_id) return -1;
    pthread_mutex_lock(&g_data_mutex);
    int res = mmap_marks_delete(student_id, course_id);
    pthread_mutex_unlock(&g_data_mutex);
    return res;
}

void data_to_json(char *buffer, size_t max_size) {
    pthread_mutex_lock(&g_data_mutex);
    size_t offset = 0;
    offset += snprintf(buffer + offset, max_size - offset, "{\n  \"students\": [\n");

    for (uint32_t i = 0; i < g_student_count; i++) {
        offset += snprintf(buffer + offset, max_size - offset,
            "    {\"id\": \"%s\", \"name\": \"%s\", \"email\": \"%s\", \"department\": \"%s\", \"year\": %d, \"section\": \"%s\"}%s\n",
            g_students[i].id, g_students[i].name, g_students[i].email,
            g_students[i].department, g_students[i].year, g_students[i].section,
            (i == g_student_count - 1) ? "" : ",");
        if (offset >= max_size - 200) break;
    }

    offset += snprintf(buffer + offset, max_size - offset, "  ],\n  \"faculty\": [\n");
    for (uint32_t i = 0; i < g_faculty_count; i++) {
        offset += snprintf(buffer + offset, max_size - offset,
            "    {\"id\": \"%s\", \"name\": \"%s\", \"email\": \"%s\", \"department\": \"%s\"}%s\n",
            g_faculty[i].id, g_faculty[i].name, g_faculty[i].email,
            g_faculty[i].department, (i == g_faculty_count - 1) ? "" : ",");
        if (offset >= max_size - 200) break;
    }

    offset += snprintf(buffer + offset, max_size - offset, "  ],\n  \"courses\": [\n");
    for (uint32_t i = 0; i < g_course_count; i++) {
        offset += snprintf(buffer + offset, max_size - offset,
            "    {\"id\": \"%s\", \"name\": \"%s\", \"credits\": %d, \"department\": \"%s\", \"semester\": %d, \"faculty_id\": \"%s\"}%s\n",
            g_courses[i].id, g_courses[i].name, g_courses[i].credits,
            g_courses[i].department, g_courses[i].semester, g_courses[i].faculty_id,
            (i == g_course_count - 1) ? "" : ",");
        if (offset >= max_size - 200) break;
    }

    snprintf(buffer + offset, max_size - offset, "  ]\n}");
    pthread_mutex_unlock(&g_data_mutex);
}

void data_student_dashboard_to_json(const char *student_id, char *buffer, size_t max_size) {
    pthread_mutex_lock(&g_data_mutex);
    const Student *s = data_find_student_by_id(student_id);
    if (!s) {
        snprintf(buffer, max_size, "{\"error\": true, \"message\": \"Student not found\"}");
        pthread_mutex_unlock(&g_data_mutex);
        return;
    }

    size_t offset = 0;
    offset += snprintf(buffer + offset, max_size - offset,
        "{\n"
        "  \"student\": {\n"
        "    \"id\": \"%s\",\n"
        "    \"name\": \"%s\",\n"
        "    \"email\": \"%s\",\n"
        "    \"department\": \"%s\",\n"
        "    \"year\": %d,\n"
        "    \"section\": \"%s\"\n"
        "  },\n"
        "  \"courses\": [\n",
        s->id, s->name, s->email, s->department, s->year, s->section);

    /* Collect enrolled courses for this student */
    int enrolled_indices[MAX_COURSES];
    int enrolled_count = 0;
    int total_credits = 0;

    for (uint32_t i = 0; i < g_marks_count; i++) {
        if (strcmp(g_marks[i].student_id, student_id) == 0) {
            for (uint32_t c = 0; c < g_course_count; c++) {
                if (strcmp(g_courses[c].id, g_marks[i].course_id) == 0) {
                    enrolled_indices[enrolled_count++] = c;
                    total_credits += g_courses[c].credits;
                    break;
                }
            }
        }
    }

    for (int i = 0; i < enrolled_count; i++) {
        int c = enrolled_indices[i];
        const Faculty *fac = data_find_faculty_by_id(g_courses[c].faculty_id);
        const char *fac_name = fac ? fac->name : "Faculty";
        offset += snprintf(buffer + offset, max_size - offset,
            "    {\"id\": \"%s\", \"name\": \"%s\", \"credits\": %d, \"department\": \"%s\", \"semester\": %d, \"faculty_name\": \"%s\"}%s\n",
            g_courses[c].id, g_courses[c].name, g_courses[c].credits,
            g_courses[c].department, g_courses[c].semester, fac_name,
            (i == enrolled_count - 1) ? "" : ",");
    }

    offset += snprintf(buffer + offset, max_size - offset, "  ],\n  \"marks\": [\n");

    float sum_marks_pct = 0.0f;
    int marks_counted = 0;
    int marks_first = 1;
    for (uint32_t i = 0; i < g_marks_count; i++) {
        if (strcmp(g_marks[i].student_id, student_id) == 0) {
            const Course *c = data_find_course_by_id(g_marks[i].course_id);
            const char *c_name = c ? c->name : g_marks[i].course_id;

            if (!marks_first) {
                offset += snprintf(buffer + offset, max_size - offset, ",\n");
            }
            marks_first = 0;
            offset += snprintf(buffer + offset, max_size - offset,
                "    {\"course_id\": \"%s\", \"course_name\": \"%s\", \"midterm\": %.1f, \"endterm\": %.1f, \"quiz\": %.1f, \"total_pct\": %.2f, \"grade\": \"%s\"}",
                g_marks[i].course_id, c_name, g_marks[i].midterm, g_marks[i].endterm, g_marks[i].quiz,
                g_marks[i].total_pct, g_marks[i].grade);
            sum_marks_pct += g_marks[i].total_pct;
            marks_counted++;
        }
    }

    offset += snprintf(buffer + offset, max_size - offset, "  ],\n  \"attendance\": [\n");

    int total_attended = 0;
    int total_classes = 0;
    int att_counted = 0;
    int attendance_first = 1;

    for (uint32_t i = 0; i < g_attendance_count; i++) {
        if (strcmp(g_attendance[i].student_id, student_id) == 0) {
            const Course *c = data_find_course_by_id(g_attendance[i].course_id);
            const char *c_name = c ? c->name : g_attendance[i].course_id;

            if (!attendance_first) {
                offset += snprintf(buffer + offset, max_size - offset, ",\n");
            }
            attendance_first = 0;
            offset += snprintf(buffer + offset, max_size - offset,
                "    {\"course_id\": \"%s\", \"course_name\": \"%s\", \"attended\": %d, \"total\": %d, \"percentage\": %.1f}",
                g_attendance[i].course_id, c_name, g_attendance[i].attended_classes,
                g_attendance[i].total_classes, g_attendance[i].percentage);
            total_attended += g_attendance[i].attended_classes;
            total_classes += g_attendance[i].total_classes;
            att_counted++;
        }
    }

    float overall_marks_avg = (marks_counted > 0) ? (sum_marks_pct / marks_counted) : 0.0f;
    float overall_att_pct = (total_classes > 0) ? (((float)total_attended / total_classes) * 100.0f) : 0.0f;
    float gpa = overall_marks_avg / 10.0f; /* 10-point scale */

    offset += snprintf(buffer + offset, max_size - offset,
        "  ],\n"
        "  \"overall_stats\": {\n"
        "    \"average_marks_pct\": %.2f,\n"
        "    \"gpa\": %.2f,\n"
        "    \"overall_attendance_pct\": %.1f,\n"
        "    \"total_attended\": %d,\n"
        "    \"total_classes\": %d,\n"
        "    \"total_credits\": %d,\n"
        "    \"enrolled_courses\": %d\n"
        "  }\n"
        "}",
        overall_marks_avg, gpa, overall_att_pct, total_attended, total_classes, total_credits, enrolled_count);
    pthread_mutex_unlock(&g_data_mutex);
}

void data_faculty_dashboard_to_json(const char *faculty_id, char *buffer, size_t max_size) {
    pthread_mutex_lock(&g_data_mutex);
    const Faculty *f = data_find_faculty_by_id(faculty_id);
    if (!f) {
        snprintf(buffer, max_size, "{\"error\": true, \"message\": \"Faculty not found\"}");
        pthread_mutex_unlock(&g_data_mutex);
        return;
    }

    size_t offset = 0;
    offset += snprintf(buffer + offset, max_size - offset,
        "{\n"
        "  \"faculty\": {\n"
        "    \"id\": \"%s\",\n"
        "    \"name\": \"%s\",\n"
        "    \"email\": \"%s\",\n"
        "    \"department\": \"%s\"\n"
        "  },\n"
        "  \"courses\": [\n",
        f->id, f->name, f->email, f->department);

    /* Collect courses taught by this faculty */
    char taught_course_ids[MAX_COURSES][16];
    int taught_count = 0;

    for (uint32_t i = 0; i < g_course_count; i++) {
        if (strcmp(g_courses[i].faculty_id, faculty_id) == 0 && taught_count < MAX_COURSES) {
            snprintf(taught_course_ids[taught_count], sizeof(taught_course_ids[taught_count]), "%.15s", g_courses[i].id);
            taught_count++;
        }
    }

    for (int i = 0; i < taught_count; i++) {
        const Course *c = data_find_course_by_id(taught_course_ids[i]);
        /* Count students enrolled in this course */
        int enrolled = 0;
        for (uint32_t m = 0; m < g_marks_count; m++) {
            if (strcmp(g_marks[m].course_id, taught_course_ids[i]) == 0) enrolled++;
        }

        offset += snprintf(buffer + offset, max_size - offset,
            "    {\"id\": \"%s\", \"name\": \"%s\", \"credits\": %d, \"department\": \"%s\", \"semester\": %d, \"enrolled_students\": %d}%s\n",
            c->id, c->name, c->credits, c->department, c->semester, enrolled,
            (i == taught_count - 1) ? "" : ",");
    }

    offset += snprintf(buffer + offset, max_size - offset, "  ],\n  \"students\": [\n");

    /* Distinct students taught by this faculty */
    char student_ids[MAX_STUDENTS][16];
    int distinct_students = 0;

    for (uint32_t i = 0; i < g_marks_count; i++) {
        int is_taught = 0;
        for (int t = 0; t < taught_count; t++) {
            if (strcmp(g_marks[i].course_id, taught_course_ids[t]) == 0) {
                is_taught = 1;
                break;
            }
        }
        if (is_taught) {
            int exists = 0;
            for (int s = 0; s < distinct_students; s++) {
                if (strcmp(student_ids[s], g_marks[i].student_id) == 0) {
                    exists = 1;
                    break;
                }
            }
            if (!exists && distinct_students < MAX_STUDENTS) {
                snprintf(student_ids[distinct_students], sizeof(student_ids[distinct_students]), "%.15s", g_marks[i].student_id);
                distinct_students++;
            }
        }
    }

    for (int i = 0; i < distinct_students; i++) {
        const Student *s = data_find_student_by_id(student_ids[i]);
        if (s) {
            offset += snprintf(buffer + offset, max_size - offset,
                "    {\"id\": \"%s\", \"name\": \"%s\", \"email\": \"%s\", \"department\": \"%s\", \"year\": %d, \"section\": \"%s\"}%s\n",
                s->id, s->name, s->email, s->department, s->year, s->section,
                (i == distinct_students - 1) ? "" : ",");
        }
    }

    offset += snprintf(buffer + offset, max_size - offset, "  ],\n  \"marks\": [\n");

    float class_marks_sum = 0.0f;
    int class_marks_count = 0;
    float max_mark = 0.0f;
    float min_mark = 100.0f;

    int marks_first = 1;
    for (uint32_t i = 0; i < g_marks_count; i++) {
        int is_taught = 0;
        for (int t = 0; t < taught_count; t++) {
            if (strcmp(g_marks[i].course_id, taught_course_ids[t]) == 0) {
                is_taught = 1;
                break;
            }
        }
        if (is_taught) {
            const Student *s = data_find_student_by_id(g_marks[i].student_id);
            const Course *c = data_find_course_by_id(g_marks[i].course_id);
            const char *s_name = s ? s->name : g_marks[i].student_id;
            const char *c_name = c ? c->name : g_marks[i].course_id;

            if (!marks_first) offset += snprintf(buffer + offset, max_size - offset, ",\n");
            marks_first = 0;

            offset += snprintf(buffer + offset, max_size - offset,
                "    {\"student_id\": \"%s\", \"student_name\": \"%s\", \"course_id\": \"%s\", \"course_name\": \"%s\", \"midterm\": %.1f, \"endterm\": %.1f, \"quiz\": %.1f, \"total_pct\": %.2f, \"grade\": \"%s\"}",
                g_marks[i].student_id, s_name, g_marks[i].course_id, c_name,
                g_marks[i].midterm, g_marks[i].endterm, g_marks[i].quiz,
                g_marks[i].total_pct, g_marks[i].grade);

            if (strcmp(g_marks[i].grade, "-") != 0) {
                class_marks_sum += g_marks[i].total_pct;
                if (g_marks[i].total_pct > max_mark) max_mark = g_marks[i].total_pct;
                if (g_marks[i].total_pct < min_mark) min_mark = g_marks[i].total_pct;
                class_marks_count++;
            }
        }
    }

    offset += snprintf(buffer + offset, max_size - offset, "\n  ],\n  \"attendance\": [\n");

    float class_att_sum = 0.0f;
    int class_att_count = 0;
    int att_first = 1;

    for (uint32_t i = 0; i < g_attendance_count; i++) {
        int is_taught = 0;
        for (int t = 0; t < taught_count; t++) {
            if (strcmp(g_attendance[i].course_id, taught_course_ids[t]) == 0) {
                is_taught = 1;
                break;
            }
        }
        if (is_taught) {
            const Student *s = data_find_student_by_id(g_attendance[i].student_id);
            const Course *c = data_find_course_by_id(g_attendance[i].course_id);
            const char *s_name = s ? s->name : g_attendance[i].student_id;
            const char *c_name = c ? c->name : g_attendance[i].course_id;

            if (!att_first) offset += snprintf(buffer + offset, max_size - offset, ",\n");
            att_first = 0;

            offset += snprintf(buffer + offset, max_size - offset,
                "    {\"student_id\": \"%s\", \"student_name\": \"%s\", \"course_id\": \"%s\", \"course_name\": \"%s\", \"attended\": %d, \"total\": %d, \"percentage\": %.1f}",
                g_attendance[i].student_id, s_name, g_attendance[i].course_id, c_name,
                g_attendance[i].attended_classes, g_attendance[i].total_classes, g_attendance[i].percentage);

            if (g_attendance[i].total_classes > 0) {
                class_att_sum += g_attendance[i].percentage;
                class_att_count++;
            }
        }
    }

    float class_avg_marks = (class_marks_count > 0) ? (class_marks_sum / class_marks_count) : 0.0f;
    float class_avg_att = (class_att_count > 0) ? (class_att_sum / class_att_count) : 0.0f;
    if (min_mark > 100.0f) min_mark = 0.0f;

    offset += snprintf(buffer + offset, max_size - offset,
        "\n  ],\n"
        "  \"stats\": {\n"
        "    \"assigned_courses\": %d,\n"
        "    \"total_students\": %d,\n"
        "    \"class_avg_marks\": %.2f,\n"
        "    \"class_avg_attendance\": %.1f,\n"
        "    \"highest_marks\": %.2f,\n"
        "    \"lowest_marks\": %.2f\n"
        "  }\n"
        "}",
        taught_count, distinct_students, class_avg_marks, class_avg_att, max_mark, min_mark);
    pthread_mutex_unlock(&g_data_mutex);
}
