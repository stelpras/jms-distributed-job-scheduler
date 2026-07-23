#ifndef JOBEXEC_H
#define JOBEXEC_H

#include "job.h"

// fork+exec the job, redirecting its stdout/stderr into files under `outdir`.
// Returns the child pid in the parent, or -1 on fork failure.
pid_t job_spawn(job_t *j, const char *outdir);

#endif