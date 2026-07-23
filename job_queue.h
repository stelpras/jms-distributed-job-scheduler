#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <pthread.h>
#include "job.h"

typedef struct {
    job_t *head, *tail;
    int count;
    int shutting_down;          // set at shutdown so blocked workers wake and exit
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
} job_queue;

void   queue_init(job_queue *q);
void   queue_destroy(job_queue *q);
void   queue_push(job_queue *q, job_t *j);     // producer (handler threads)
job_t *queue_pop(job_queue *q);                // consumer (workers); NULL if shutdown+empty
void   queue_shutdown(job_queue *q);           // wake all blocked workers

#endif