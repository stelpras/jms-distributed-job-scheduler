#define _GNU_SOURCE
#include "protocol.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

ssize_t read_all(int fd, void *buf, size_t n) {
    size_t total = 0;
    char *p = buf;
    while (total < n) {
        ssize_t r = read(fd, p + total, n - total);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        total += r;
    }
    return total;
}

ssize_t write_all(int fd, const void *buf, size_t n) {
    size_t total = 0;
    const char *p = buf;
    while (total < n) {
        ssize_t w = write(fd, p + total, n - total);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        total += w;
    }
    return total;
}

ssize_t send_all(int fd, const void *buf, size_t n) {
    size_t total = 0;
    const char *p = buf;
    while (total < n) {
        ssize_t w = send(fd, p + total, n - total, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        total += w;
    }
    return total;
}

ssize_t read_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    int got = 0;
    char c;
    while (i < maxlen - 1) {
        ssize_t r = read(fd, &c, 1);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;            // peer closed
        got = 1;
        if (c == '\n') { buf[i] = '\0'; return (ssize_t)i; }
        buf[i++] = c;
    }
    buf[i] = '\0';
    if (!got) return -1;
    return (ssize_t)i;
}

int tokenize(char *line, char **argv, int max) {
    int n = 0;
    char *save = NULL;            // strtok_r: reentrant, safe in handler threads
    char *tok = strtok_r(line, " \t", &save);
    while (tok && n < max - 1) {
        argv[n++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    argv[n] = NULL;
    return n;
}

void perror_exit(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}