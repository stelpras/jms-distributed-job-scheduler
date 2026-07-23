#ifndef JOB_TABLE_H
#define JOB_TABLE_H

#include <pthread.h>
#include "job.h"

// all jobs ever submitted, in submit order. The same job_t* also lives
// in the queue until a worker pops it; the table owns the object.
typedef struct {
    job_t **jobs;
    int count;
    int capacity;
    int next_jobid;                  // 1, 2, 3, ...
    pthread_mutex_t mutex;
    pthread_cond_t cond_changed;     // signaled on any state change
} job_table;

void   table_init(job_table *t);
void   table_destroy(job_table *t);   // frees every job

// create a job, assign the next id, store it. Returns the new job
// (caller pushes it to the queue). The id is written to *out_id.
job_t *table_add(job_table *t, char **argv, int *out_id);

// worker state transitions (each locks, updates, signals cond_changed).
void table_set_active(job_table *t, job_t *j, pid_t pid);
void table_set_finished(job_table *t, job_t *j);

// all append into `buf` (size n) the textual reply, locking internally.
void table_status(job_table *t, int jobid, char *buf, size_t n);
void table_status_all(job_table *t, int secs, char *buf, size_t n);  // secs<=0: all
void table_show_active(job_table *t, char *buf, size_t n);
void table_show_finished(job_table *t, char *buf, size_t n);

void table_stats(job_table *t, int *finished, int *running, int *queued);

#endif