CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic

client-4: 4/client.c common.h
	$(CC) $(CFLAGS) 4/client.c -o client-4

server-4: 4/server.c common.h
	$(CC) $(CFLAGS) 4/server.c -o server-4

mark4: client-4 server-4

clean:
	rm -f client-4 server-4
