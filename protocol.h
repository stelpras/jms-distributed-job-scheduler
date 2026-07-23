#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>

#define MSG_MAX 4096

ssize_t read_all(int fd, void *buf, size_t n);
ssize_t write_all(int fd, const void *buf, size_t n);

// send() with MSG_NOSIGNAL: client disconnect won't kill us with SIGPIPE
ssize_t send_all(int fd, const void *buf, size_t n);

// returns line length (0 = empty line), -1 on EOF/error
ssize_t read_line(int fd, char *buf, size_t maxlen);

int tokenize(char *line, char **argv, int max);
void perror_exit(const char *message);

#endif