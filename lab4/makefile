CC = g++
CFLAGS = -Wall -pthread -g

all: server client

server: server.cpp format.hpp
	$(CC) $(CFLAGS) server.cpp -o server

client: client.cpp format.hpp
	$(CC) $(CFLAGS) client.cpp -o client

clean:
	rm -f server client

run-server: server
	./server

run-client: client
	./client

.PHONY: all clean run-server run-client
