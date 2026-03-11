CC = gcc
CFLAGS = -Wall -Wextra -std=c11 
TARGETS = server client

all: $(TARGETS)

server: server.cpp
	$(CC) $(CFLAGS) -o $@ $^

client: client.cpp
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS)

.PHONY: all clean
