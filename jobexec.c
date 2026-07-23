#include "jobexec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

// Build "<base>/outputs_<jobid>_<pid>_<YYYYMMDD>_<HHMMSS>" into dst.
static void make_dirname(char *dst, size_t n, const char *base,
                         int jobid, pid_t pid) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(dst, n, "%s/outputs_%d_%d_%04d%02d%02d_%02d%02d%02d",
             base, jobid, (int)pid,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// Open a file inside dir and redirect fd (1 or 2) to it. Returns 0, or -1 on error.
static int redirect(const char *dir, const char *name, int fd) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int f = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f < 0) return -1;
    if (dup2(f, fd) < 0) { close(f); return -1; }
    close(f);
    return 0;
}

pid_t job_spawn(job_t *j, const char *base) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // child: now we know our own pid, so the dir name can include it
        char dir[1024], sout[64], serr[64];
        make_dirname(dir, sizeof(dir), base, j->jobid, getpid());

        // if output setup fails, exit before exec so the failure is visible
        if (mkdir(dir, 0755) < 0 && errno != EEXIST)
            _exit(126);

        snprintf(sout, sizeof(sout), "stdout_%d", j->jobid);
        snprintf(serr, sizeof(serr), "stderr_%d", j->jobid);
        if (redirect(dir, sout, 1) < 0) _exit(126);
        if (redirect(dir, serr, 2) < 0) _exit(126);

        execvp(j->argv[0], j->argv);
        _exit(127);   // exec failed: leave a non-zero code, don't run parent cleanup
    }

    return pid;   // parent
}