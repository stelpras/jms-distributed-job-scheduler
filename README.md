SYSTEMS PROGRAMMING

STYLIANOS PRASIANAKIS 

# jms - Job Management System (hw2 K24)

## FILES
  protocol.c / protocol.h     communication + parsing helpers
  job.c / job.h               job object (deep-copies argv)
  job_queue.c / job_queue.h   producer/consumer queue (mutex + cond)
  job_table.c / job_table.h   all jobs + state + status queries
  worker.c / worker.h         worker thread function (thread pool)
  jobexec.c / jobexec.h       fork/exec + output redirection
  jms_coord.c                 server: sockets, worker pool, handlers
  jms_console.c               client: connect, send commands, print
  Makefile                    separate compilation, links with -pthread

## COMPILATION
```
make
```

## CLEAN
```
make clean
```

## EXECUTION
  Coord terminal:
```
./jms_coord -p <port> -l <path> -n <workers>
```
  Console terminal:
```
./jms_console -h <host> -p <port> [-o <operations_file>]
```

  The console sends the commands in the operations file (if -o is given),
  then reads further commands from standard input. If -o is omitted it
  reads from standard input from the start.

## PROTOCOL
  Text-based over TCP. Each line ends with '\n', which marks the
  message boundary (TCP does not preserve boundaries).

  A command is one line. A reply is one or more body lines followed
  by an empty line that marks the end of the reply. This lets a
  single command return multiple lines (e.g. status-all, show-active)
  while the console knows when the full reply has arrived.

  Reading is byte-by-byte until '\n' (read_line); an empty line
  returns 0, EOF/error returns -1. Sending uses send_all, which
  loops over send() with MSG_NOSIGNAL so a client that disconnects
  mid-write does not kill the coord with SIGPIPE. A client
  disconnect is detected when read() returns 0; the console likewise
  exits when the coord closes the socket (e.g. after shutdown).

## THREADING MODEL
  The coord creates N worker threads once at startup (the -n value)
  and reuses them for all jobs. The main thread runs the accept loop;
  each connected client is served by its own detached handler thread,
  so multiple consoles can be connected at the same time.

  Two shared structures, each protected by its own mutex:
    - job queue: handler threads push submitted jobs (producer),
      workers pop them (consumer). A condition variable signals
      "queue not empty"; workers block on it instead of busy-waiting.
    - job table: holds every job ever submitted (id, pid, state,
      submit/start/end times). The table owns the job objects and
      assigns increasing JobIDs.

  The same job_t* lives in both the queue and the table. The table
  mutex protects the job's mutable fields (state, pid, times); the
  queue only touches the linking pointer. Jobs are freed once, by
  the table.

  Synchronization follows the monitor style from the lectures:
  lock -> while(!condition) cond_wait -> unlock + signal/broadcast.
  No busy-waiting.

## STARTUP
  The port given with -p is read into an int and passed to htons() when
  filling sockaddr_in; the coord bind()s and listen()s on it, while the
  console connect()s to the same port (so both must use the same value).
  The coord checks bind() on the requested port and exits with a clear
  error message if it fails (e.g. port already in use). SO_REUSEADDR is
  set before bind() so restarts are not blocked by TIME_WAIT. On startup
  it also removes any leftover outputs_* directories under the -l path
  (from previous runs), so old output does not interfere with the
  current run.

## COMMANDS
  submit <job>     enqueue a job; returns its JobID immediately
                   (does not block until the job finishes)
  status <id>      one job's state: Active (running for N seconds,
                   counted from start of execution), Finished, or
                   Queued; "JobID <id> not found" if unknown
  status-all [n]   all jobs; with n, only those submitted in the
                   last n seconds
  show-active      JobIDs of currently running jobs
  show-finished    JobIDs of completed jobs
  show-workers     each worker's thread id, state (idle or the JobID
                   it runs), and how many jobs it has served
  shutdown         stop accepting new clients, let running jobs
                   finish (waitpid), drop queued jobs, join all
                   workers, then print summary statistics and exit

  No shell parsing: pipes, redirection and quotes are not supported.

## JOB EXECUTION
  A worker pops a job, calls fork(), and the child creates its output
  directory, redirects stdout/stderr into files inside it, and
  execvp()s the command. The worker waitpid()s the child, so no
  zombies are left, then marks the job finished. If output setup fails
  (cannot create the directory or open the output files) the child
  exits with 126; if execvp fails (command not found) it exits with 127.
  In both cases the directory exists and the job is marked finished.

  At most N jobs (N = workers) run at once; further jobs wait in the
  queue until a worker is free. Workers are reused: after a job
  finishes, the same thread takes the next queued job.

  Output directory (under the -l path):
    outputs_<jobid>_<pid>_<date>_<time>    e.g. outputs_3_1234_20260420_175000
  containing:
    stdout_<jobid>   the job's standard output
    stderr_<jobid>   the job's standard error

## SHUTDOWN
  On "shutdown" the coord stops accepting new connections (a self-pipe
  wakes the select-based accept loop), sets the queue's shutting_down
  flag and broadcasts so every worker wakes. Idle workers exit at once;
  busy workers finish their current child with waitpid and then exit.
  Jobs still waiting in the queue are dropped. The main thread joins
  all workers, closes the listening socket, and prints:
    Served X jobs, Y were running, Z were still queued
  where Y/Z are counted at shutdown time and X after the running jobs
  have drained. No CTRL+C is needed; the coord exits on its own.

  Self-pipe design: the main thread blocks in select() on both the
  listening socket and the read end of a pipe (g_shutdown_pipe). The
  shutdown command runs in a handler thread, so it cannot directly
  break the main thread out of select(); instead it sets g_shutdown
  and writes one byte to the pipe, which makes select() return so the
  main thread sees the flag and leaves the accept loop. g_shutdown is
  declared volatile sig_atomic_t because it is written by one thread
  and read by another.

## MANUAL TEST
  Start the coord, then connect a console and type commands (one per
  line), or pass them in a file with -o. Example:
```
Terminal 1:  ./jms_coord -p 9000 -l ./out -n 4
Terminal 2:  ./jms_console -h localhost -p 9000
             submit sleep 20
             submit sleep 20
             ... (more jobs than workers)
             show-active
             show-workers
             status 1
             shutdown
```
  With more jobs than workers, exactly N run at once (Active) and the
  rest are Queued; after they finish, show-finished lists them all and
  output files appear under ./out/outputs_<...>/. "shutdown" prints
  "Served X jobs, Y were running, Z were still queued" and the coord
  exits.

## AUTOMATED TEST
  test_jms.sh provides a smoke test that starts the coordinator in the
  background, connects a console, submits several jobs (a mix of quick
  commands and longer sleeps to exercise queueing), verifies that output
  directories and files are created, checks status and worker information,
  then gracefully shuts down the system.

  Running it:
```
chmod +x test_jms.sh
./test_jms.sh
```

  The script uses time bounds (30-second coordinator timeout, 15-second
  console timeouts) to prevent hanging. It performs 8 checks:
    - Coordinator builds successfully
    - Coordinator starts and binds to the port
    - Console can connect and submit jobs
    - Coordinator handles job execution and queuing
    - Status commands return job information
    - Output directories with stdout/stderr files are created
    - Shutdown command works and coordinator exits cleanly
    - Valgrind reports no memory errors (if available)

  After the test completes, the script cleans up all processes, temporary
  directories, and log files. Note: this is a smoke test, not a stress
  test. For full validation, run the coordinator manually, submit many
  jobs (more than the number of workers), and verify that exactly N jobs
  run concurrently while the rest wait in the queue.
