#ifndef JOB_H
#define JOB_H

#include <sys/types.h>
#include <time.h>

typedef enum { QUEUED, ACTIVE, FINISHED } job_state;

typedef struct job {
    int jobid;
    char **argv;            // command + args, NULL-terminated
    pid_t pid;              // child pid once running (-1 before)
    job_state state;
    time_t submit_time;
    time_t start_time;      // set when execution begins
    time_t end_time;        // set when the child finishes
    struct job *next;       // for the queue's linked list
} job_t;

job_t *job_create(int jobid, char **argv);   // deep-copies argv
void   job_free(job_t *j);

#endif