#include "job.h"
#include <stdlib.h>
#include <string.h>

static char **argv_dup(char **argv) {
    int n = 0;
    while (argv[n]) n++;
    char **copy = malloc((n + 1) * sizeof(char *));
    if (!copy) return NULL;
    for (int i = 0; i < n; i++) copy[i] = strdup(argv[i]);
    copy[n] = NULL;
    return copy;
}

job_t *job_create(int jobid, char **argv) {
    job_t *j = malloc(sizeof(job_t));
    if (!j) return NULL;
    j->jobid = jobid;
    j->argv = argv_dup(argv);      // own copy: the request buffer gets reused
    j->pid = -1;
    j->state = QUEUED;
    j->submit_time = time(NULL);
    j->start_time = 0;
    j->end_time = 0;
    j->next = NULL;
    return j;
}

void job_free(job_t *j) {
    if (!j) return;
    if (j->argv) {
        for (int i = 0; j->argv[i]; i++) free(j->argv[i]);
        free(j->argv);
    }
    free(j);
}