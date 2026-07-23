#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>
#include "job_queue.h"
#include "job_table.h"

typedef struct {
    pthread_t tid;
    int idle;            // 1 = waiting, 0 = running a job
    int current_jobid;   // -1 when idle
    int served;          // jobs handled so far
} worker_info;

// Shared context passed to every worker thread.
typedef struct {
    job_queue *queue;
    job_table *table;
    worker_info *workers;
    int nworkers;
    int my_index;
    pthread_mutex_t *wmutex;
    const char *outbase;       // the -l path; where output dirs are created
} worker_ctx;

void *worker_main(void *arg);

// Build the show-workers reply into buf, locking wmutex internally.
void workers_report(worker_info *w, int n, pthread_mutex_t *wmutex,
                    char *buf, size_t blen);

#endif