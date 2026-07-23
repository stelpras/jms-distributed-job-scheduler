#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "protocol.h"

// print response lines until the empty terminator; 1 if coord closed
static int read_response(int sock) {
    char line[MSG_MAX];
    for (;;) {
        ssize_t r = read_line(sock, line, sizeof(line));
        if (r < 0) return 1;
        if (r == 0) return 0;     // end of response
        printf("%s\n", line);
    }
}

static int run_stream(int sock, FILE *in) {
    char line[MSG_MAX];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        if (send_all(sock, line, strlen(line)) < 0) return 1;
        if (send_all(sock, "\n", 1) < 0) return 1;
        if (read_response(sock) != 0) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char *host = NULL, *opfile = NULL;
    int port = -1, opt;

    while ((opt = getopt(argc, argv, "h:p:o:")) != -1) {
        switch (opt) {
            case 'h': host = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 'o': opfile = optarg; break;
            default:
                fprintf(stderr, "usage: %s -h <host> -p <port> [-o <file>]\n", argv[0]);
                exit(1);
        }
    }
    if (!host || port <= 0) {
        fprintf(stderr, "usage: %s -h <host> -p <port> [-o <file>]\n", argv[0]);
        exit(1);
    }

    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror_exit("socket");

    struct hostent *rem;
    if ((rem = gethostbyname(host)) == NULL) {
        herror("gethostbyname");
        exit(1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
    server.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        perror_exit("connect");

    // operations file first (if given), then interactive stdin
    int closed = 0;
    if (opfile) {
        FILE *fp = fopen(opfile, "r");
        if (!fp) perror_exit("fopen");
        closed = run_stream(sock, fp);
        fclose(fp);
    }
    if (!closed)
        run_stream(sock, stdin);

    close(sock);
    return 0;
}