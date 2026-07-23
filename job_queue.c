#include "job_queue.h"
#include <stdlib.h>

void queue_init(job_queue *q) {
    q->head = q->tail = NULL;
    q->count = 0;
    q->shutting_down = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_not_empty, NULL);
}

void queue_destroy(job_queue *q) {
    // jobs are owned and freed by the job table, not here
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond_not_empty);
}

void queue_push(job_queue *q, job_t *j) {
    pthread_mutex_lock(&q->mutex);
    j->next = NULL;
    if (q->tail) q->tail->next = j;
    else q->head = j;
    q->tail = j;
    q->count++;
    pthread_cond_signal(&q->cond_not_empty);   // wake one waiting worker
    pthread_mutex_unlock(&q->mutex);
}

// Monitor pattern: wait while empty AND not shutting down.
// On shutdown, queued jobs are dropped (spec: not-yet-started jobs are discarded),
// so this returns NULL as soon as shutting_down is set.
job_t *queue_pop(job_queue *q) {
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->shutting_down)
        pthread_cond_wait(&q->cond_not_empty, &q->mutex);

    if (q->shutting_down) {            // shutdown: drop whatever is still queued
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }

    job_t *j = q->head;
    q->head = j->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    j->next = NULL;
    return j;
}

void queue_shutdown(job_queue *q) {
    pthread_mutex_lock(&q->mutex);
    q->shutting_down = 1;
    pthread_cond_broadcast(&q->cond_not_empty);   // wake every idle worker
    pthread_mutex_unlock(&q->mutex);
}