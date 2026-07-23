#include "job_table.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define INIT_CAP 16

void table_init(job_table *t) {
    t->capacity = INIT_CAP;
    t->jobs = malloc(t->capacity * sizeof(job_t *));
    t->count = 0;
    t->next_jobid = 1;
    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond_changed, NULL);
}

void table_destroy(job_table *t) {
    for (int i = 0; i < t->count; i++) job_free(t->jobs[i]);
    free(t->jobs);
    pthread_mutex_destroy(&t->mutex);
    pthread_cond_destroy(&t->cond_changed);
}

job_t *table_add(job_table *t, char **argv, int *out_id) {
    pthread_mutex_lock(&t->mutex);
    int id = t->next_jobid++;
    job_t *j = job_create(id, argv);
    if (t->count == t->capacity) {              // grow if full
        int new_cap = t->capacity * 2;
        job_t **tmp = realloc(t->jobs, new_cap * sizeof(job_t *));
        if (tmp) {                              // keep old array if realloc fails
            t->jobs = tmp;
            t->capacity = new_cap;
        }
    }
    t->jobs[t->count++] = j;
    *out_id = id;
    pthread_mutex_unlock(&t->mutex);
    return j;
}

void table_set_active(job_table *t, job_t *j, pid_t pid) {
    pthread_mutex_lock(&t->mutex);
    j->pid = pid;
    j->state = ACTIVE;
    j->start_time = time(NULL);
    pthread_cond_broadcast(&t->cond_changed);
    pthread_mutex_unlock(&t->mutex);
}

void table_set_finished(job_table *t, job_t *j) {
    pthread_mutex_lock(&t->mutex);
    j->state = FINISHED;
    j->end_time = time(NULL);
    pthread_cond_broadcast(&t->cond_changed);
    pthread_mutex_unlock(&t->mutex);
}

// Append formatted text to buf without overflowing (size n, NUL-terminated).
static void appendf(char *buf, size_t n, const char *fmt, ...) {
    size_t len = strlen(buf);
    if (len >= n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, n - len, fmt, ap);
    va_end(ap);
}

// One job's status line into buf (no newline). now = current time.
static void format_status(job_t *j, time_t now, char *buf, size_t n) {
    if (j->state == ACTIVE)
        snprintf(buf, n, "JobID %d Status: Active (running for %ld seconds)",
                 j->jobid, (long)(now - j->start_time));
    else if (j->state == FINISHED)
        snprintf(buf, n, "JobID %d Status: Finished", j->jobid);
    else
        snprintf(buf, n, "JobID %d Status: Queued (waiting in job queue)", j->jobid);
}

void table_status(job_table *t, int jobid, char *buf, size_t n) {
    pthread_mutex_lock(&t->mutex);
    job_t *found = NULL;
    for (int i = 0; i < t->count; i++)
        if (t->jobs[i]->jobid == jobid) { found = t->jobs[i]; break; }

    if (found) format_status(found, time(NULL), buf, n);
    else snprintf(buf, n, "JobID %d not found", jobid);
    pthread_mutex_unlock(&t->mutex);
}

void table_status_all(job_table *t, int secs, char *buf, size_t n) {
    pthread_mutex_lock(&t->mutex);
    time_t now = time(NULL);
    buf[0] = '\0';
    for (int i = 0; i < t->count; i++) {
        job_t *j = t->jobs[i];
        if (secs > 0 && (now - j->submit_time) > secs) continue;  // older than n sec
        char line[256];
        format_status(j, now, line, sizeof(line));
        appendf(buf, n, "%s%s", buf[0] ? "\n" : "", line);
    }
    if (buf[0] == '\0') snprintf(buf, n, "No jobs");
    pthread_mutex_unlock(&t->mutex);
}

void table_show_active(job_table *t, char *buf, size_t n) {
    pthread_mutex_lock(&t->mutex);
    snprintf(buf, n, "Active jobs:");
    for (int i = 0; i < t->count; i++)
        if (t->jobs[i]->state == ACTIVE)
            appendf(buf, n, "\nJobID %d", t->jobs[i]->jobid);
    pthread_mutex_unlock(&t->mutex);
}

void table_show_finished(job_table *t, char *buf, size_t n) {
    pthread_mutex_lock(&t->mutex);
    snprintf(buf, n, "Finished jobs:");
    for (int i = 0; i < t->count; i++)
        if (t->jobs[i]->state == FINISHED)
            appendf(buf, n, "\nJobID %d", t->jobs[i]->jobid);
    pthread_mutex_unlock(&t->mutex);
}


void table_stats(job_table *t, int *finished, int *running, int *queued) {
    pthread_mutex_lock(&t->mutex);
    *finished = *running = *queued = 0;
    for (int i = 0; i < t->count; i++) {
        if (t->jobs[i]->state == FINISHED) (*finished)++;
        else if (t->jobs[i]->state == ACTIVE) (*running)++;
        else (*queued)++;
    }
    pthread_mutex_unlock(&t->mutex);
}