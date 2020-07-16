CC = gcc
CFLAGS = -g -O2 -Wall -Werror

all: server.o client.o
	$(CC) $(CFLAGS) server.o -o server_control
	$(CC) $(CFLAGS) client.o -o client

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

.PHONEY: clean debug

clean:
	rm *.o
