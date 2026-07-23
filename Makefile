CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -pthread

COORD_OBJS   = jms_coord.o protocol.o job.o job_queue.o job_table.o worker.o jobexec.o
CONSOLE_OBJS = jms_console.o protocol.o

all: jms_coord jms_console

jms_coord: $(COORD_OBJS)
	$(CC) $(CFLAGS) -o jms_coord $(COORD_OBJS) $(LDFLAGS)

jms_console: $(CONSOLE_OBJS)
	$(CC) $(CFLAGS) -o jms_console $(CONSOLE_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o jms_coord jms_console