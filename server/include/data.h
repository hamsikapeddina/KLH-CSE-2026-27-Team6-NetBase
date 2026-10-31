#ifndef DATA_H
#define DATA_H

#include <stddef.h>

#define MAX_STUDENTS 64
#define MAX_FACULTY 32
#define MAX_COURSES 32
#define MAX_MARKS 128
#define MAX_ATTENDANCE 128
#define STR_LEN 64

/* Academic Entity: Student */
typedef struct {
    char id[16];
    char name[STR_LEN];
    char email[STR_LEN];
    char department[STR_LEN];
    int year;
    char section[8];
    char password[STR_LEN];
} Student;

/* Academic Entity: Faculty */
typedef struct {
    char id[16];
    char name[STR_LEN];
    char email[STR_LEN];
    char department[STR_LEN];
    char password[STR_LEN];
} Faculty;

/* Academic Entity: Course */
typedef struct {
    char id[16];
    char name[STR_LEN];
    int credits;
    char department[STR_LEN];
    int semester;
    char faculty_id[16];
} Course;

/* Academic Entity: Marks Record */
typedef struct {
    char student_id[16];
    char course_id[16];
    float midterm;   /* out of 50 */
    float endterm;   /* out of 100 */
    float quiz;      /* out of 25 */
    float total_pct; /* (midterm + endterm + quiz) / 175 * 100 */
    char grade[4];   /* A+, A, B, C, D, F */
} MarkRecord;

/* Academic Entity: Attendance Record */
typedef struct {
    char student_id[16];
    char course_id[16];
    int attended_classes;
    int total_classes;
    float percentage;
} AttendanceRecord;

/* Data store operations */
int data_init(void);

int data_get_student_count(void);
int data_get_faculty_count(void);
int data_get_course_count(void);
int data_get_marks_count(void);
int data_get_attendance_count(void);

const Student* data_get_students(void);
const Faculty* data_get_faculty(void);
const Course* data_get_courses(void);
const MarkRecord* data_get_marks(void);
const AttendanceRecord* data_get_attendance(void);

const Student* data_find_student_by_id(const char *id);
const Faculty* data_find_faculty_by_id(const char *id);
const Course* data_find_course_by_id(const char *id);

/* Authentication */
int data_authenticate(const char *role, const char *id, const char *password, void **out_user);

/* Update operations and CRUD */
int data_update_marks(const char *student_id, const char *course_id, float midterm, float endterm, float quiz, MarkRecord *out_record);
int data_update_attendance(const char *student_id, const char *course_id, int attended, int total, AttendanceRecord *out_record);
int data_insert_student(const Student *s);
int data_update_student(const Student *s);
int data_delete_student(const char *id);
int data_insert_faculty(const Faculty *f);
int data_update_faculty(const Faculty *f);
int data_delete_faculty(const char *id);
int data_delete_marks(const char *student_id, const char *course_id);

/* JSON serialization helpers for HTTP responses */
void data_to_json(char *buffer, size_t max_size);
void data_student_dashboard_to_json(const char *student_id, char *buffer, size_t max_size);
void data_faculty_dashboard_to_json(const char *faculty_id, char *buffer, size_t max_size);

#endif /* DATA_H */
