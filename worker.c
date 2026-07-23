#include "worker.h"
#include "jobexec.h"
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>

void *worker_main(void *arg) {
    worker_ctx *ctx = arg;
    worker_info *me = &ctx->workers[ctx->my_index];

    pthread_mutex_lock(ctx->wmutex);
    me->tid = pthread_self();      // recorded for show-workers
    pthread_mutex_unlock(ctx->wmutex);

    for (;;) {
        job_t *j = queue_pop(ctx->queue);   // blocks; NULL on shutdown+empty
        if (!j) break;

        pthread_mutex_lock(ctx->wmutex);
        me->idle = 0;
        me->current_jobid = j->jobid;
        pthread_mutex_unlock(ctx->wmutex);

        pid_t pid = job_spawn(j, ctx->outbase);   // fork/exec
        if (pid > 0) {
            table_set_active(ctx->table, j, pid);
            int status;
            waitpid(pid, &status, 0);             // wait for the child to finish
        }
        table_set_finished(ctx->table, j);

        pthread_mutex_lock(ctx->wmutex);
        me->idle = 1;
        me->current_jobid = -1;
        me->served++;
        pthread_mutex_unlock(ctx->wmutex);
    }
    return NULL;
}


void workers_report(worker_info *w, int n, pthread_mutex_t *wmutex,
                    char *buf, size_t blen) {
    pthread_mutex_lock(wmutex);
    snprintf(buf, blen, "Worker TID, State, Served:");
    for (int i = 0; i < n; i++) {
        size_t len = strlen(buf);
        if (w[i].idle)
            snprintf(buf + len, blen - len, "\n0x%lx idle served %d",
                     (unsigned long)w[i].tid, w[i].served);
        else
            snprintf(buf + len, blen - len, "\n0x%lx running JobID %d served %d",
                     (unsigned long)w[i].tid, w[i].current_jobid, w[i].served);
    }
    pthread_mutex_unlock(wmutex);
}