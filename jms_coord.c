#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include "protocol.h"
#include "job_queue.h"
#include "job_table.h"
#include "worker.h"
#include <signal.h>
#include <sys/select.h>
#include <dirent.h>
#include <sys/stat.h>


static volatile sig_atomic_t g_shutdown = 0;
static int g_shutdown_pipe[2];   // [0] read end (main), [1] write end (handler)


#define MAX_ARGS 64




static job_queue   g_queue;
static job_table   g_table;
static worker_info *g_workers;
static int          g_nworkers;
static pthread_mutex_t g_wmutex = PTHREAD_MUTEX_INITIALIZER;

// recursively delete a directory and its contents (no shell, any path is safe)
static void rmrf(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;
            char child[2048];
            snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
            struct stat st;
            if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
                rmrf(child);          // recurse into subdir
            else
                unlink(child);        // remove file
        }
        closedir(d);
    }
    rmdir(path);                      // remove the (now empty) dir itself
}

// remove old outputs_* directories left under base from previous runs
static void clean_outputs(const char *base) {
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "outputs_", 8) != 0) continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", base, e->d_name);
        rmrf(path);
    }
    closedir(d);
}

// response = body lines, then an empty line marking the end
static int send_response(int fd, const char *body) {
    if (body && body[0] != '\0') {
        if (send_all(fd, body, strlen(body)) < 0) return -1;
        if (send_all(fd, "\n", 1) < 0) return -1;
    }
    if (send_all(fd, "\n", 1) < 0) return -1;
    return 0;
}

// returns 1 if shutdown was requested
static int handle_command(int fd, char *line) {
    char *argv[MAX_ARGS];
    int n = tokenize(line, argv, MAX_ARGS);
    if (n == 0) { send_response(fd, ""); return 0; }

    char resp[MSG_MAX];

    if (strcmp(argv[0], "submit") == 0) {
        if (n < 2) send_response(fd, "Error: submit needs a command");
        else {
            int id;
            job_t *j = table_add(&g_table, argv + 1, &id);  // argv+1: skip "submit"
            queue_push(&g_queue, j);
            snprintf(resp, sizeof(resp), "JobID: %d", id);
            send_response(fd, resp);
        }
    }
    else if (strcmp(argv[0], "status") == 0) {
        if (n < 2) send_response(fd, "Error: status needs a JobID");
        else {
            table_status(&g_table, atoi(argv[1]), resp, sizeof(resp));
            send_response(fd, resp);
        }
    }
    else if (strcmp(argv[0], "status-all") == 0) {
        int secs = (n >= 2) ? atoi(argv[1]) : 0;   // optional [n]: last n seconds
        table_status_all(&g_table, secs, resp, sizeof(resp));
        send_response(fd, resp);
    }
    else if (strcmp(argv[0], "show-active") == 0) {
        table_show_active(&g_table, resp, sizeof(resp));
        send_response(fd, resp);
    }
    else if (strcmp(argv[0], "show-finished") == 0) {
        table_show_finished(&g_table, resp, sizeof(resp));
        send_response(fd, resp);
    }
    else if (strcmp(argv[0], "show-workers") == 0) {
        workers_report(g_workers, g_nworkers, &g_wmutex, resp, sizeof(resp));
        send_response(fd, resp);
    }
    else if (strcmp(argv[0], "shutdown") == 0) {
        g_shutdown = 1;
        (void)write(g_shutdown_pipe[1], "x", 1);   // wake the main thread's select()
        return 1;
    }
    else {
        snprintf(resp, sizeof(resp), "Unknown command: %s", argv[0]);
        send_response(fd, resp);
    }
    return 0;
}

typedef struct { int fd; } handler_arg;

// one detached thread per connected client
static void *handler_main(void *arg) {
    int fd = ((handler_arg *)arg)->fd;
    free(arg);
    pthread_detach(pthread_self());   // self-cleanup, no join needed

    char line[MSG_MAX];
    for (;;) {
        ssize_t r = read_line(fd, line, sizeof(line));
        if (r < 0) break;                       // client disconnected
        if (handle_command(fd, line)) break;    // shutdown
    }
    close(fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = -1, nworkers = -1, opt;
    char *path = NULL;

    while ((opt = getopt(argc, argv, "p:l:n:")) != -1) {
        switch (opt) {
            case 'p': port = atoi(optarg); break;
            case 'l': path = optarg; break;
            case 'n': nworkers = atoi(optarg); break;
            default:
                fprintf(stderr, "usage: %s -p <port> -l <path> -n <workers>\n", argv[0]);
                exit(1);
        }
    }
    if (port <= 0 || !path || nworkers <= 0) {
        fprintf(stderr, "usage: %s -p <port> -l <path> -n <workers>\n", argv[0]);
        exit(1);
    }

    if (mkdir(path, 0755) < 0 && errno != EEXIST)   // ensure output base dir exists
        perror_exit("mkdir");
    clean_outputs(path);   // remove leftover output dirs from previous runs

    int listen_fd;
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror_exit("socket");

    int reuse = 1;   // quick restart on same port (avoid TIME_WAIT bind failure)
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
        perror_exit("bind");
    if (listen(listen_fd, 5) < 0)
        perror_exit("listen");

    // shared structures + worker thread pool (created once at startup)
    queue_init(&g_queue);
    table_init(&g_table);
    g_nworkers = nworkers;
    g_workers = malloc(nworkers * sizeof(worker_info));

    pthread_t   *wtids = malloc(nworkers * sizeof(pthread_t));
    worker_ctx  *ctxs  = malloc(nworkers * sizeof(worker_ctx));
    for (int i = 0; i < nworkers; i++) {
        g_workers[i].idle = 1;
        g_workers[i].current_jobid = -1;
        g_workers[i].served = 0;
        ctxs[i].queue = &g_queue;
        ctxs[i].table = &g_table;
        ctxs[i].workers = g_workers;
        ctxs[i].nworkers = nworkers;
        ctxs[i].my_index = i;
        ctxs[i].wmutex = &g_wmutex;
        ctxs[i].outbase = path;
        pthread_create(&wtids[i], NULL, worker_main, &ctxs[i]);
    }

    // self-pipe so the shutdown command can wake the blocking accept loop
    if (pipe(g_shutdown_pipe) < 0) perror_exit("pipe");

    // accept loop: wait on the listening socket AND the shutdown pipe
    while (!g_shutdown) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        FD_SET(g_shutdown_pipe[0], &rfds);
        int maxfd = (listen_fd > g_shutdown_pipe[0]) ? listen_fd : g_shutdown_pipe[0];

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror_exit("select");
        }
        if (FD_ISSET(g_shutdown_pipe[0], &rfds)) break;   // shutdown requested

        if (FD_ISSET(listen_fd, &rfds)) {
            int client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                perror_exit("accept");
            }
            handler_arg *ha = malloc(sizeof(handler_arg));
            ha->fd = client_fd;
            pthread_t htid;
            pthread_create(&htid, NULL, handler_main, ha);
        }
    }

    // stop accepting new connections
    close(listen_fd);

    // count running/queued at shutdown time (before workers drain)
    int dummy, running, queued;
    table_stats(&g_table, &dummy, &running, &queued);

    // wake every worker: idle ones exit, busy ones finish their waitpid first
    queue_shutdown(&g_queue);

    for (int i = 0; i < nworkers; i++)
        pthread_join(wtids[i], NULL);

    // count finished AFTER workers drained (the running ones are now done)
    int finished;
    table_stats(&g_table, &finished, &dummy, &dummy);

    printf("Served %d jobs, %d were running, %d were still queued\n",
           finished, running, queued);

    queue_destroy(&g_queue);
    table_destroy(&g_table);
    free(g_workers);
    free(wtids);
    free(ctxs);
    close(g_shutdown_pipe[0]);
    close(g_shutdown_pipe[1]);
    return 0;
}