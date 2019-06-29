CC=gcc
CFLAGS=-O3 -Wall
LIBS=-pthread
SRC=src

all: server client

server: server.o
	$(CC) $(CFLAGS) $(LIBS) -o server server.o
client: client.o
	$(CC) $(CFLAGS) $(LIBS) -o client client.o
	
server.o: $(SRC)/server.c $(SRC)/server.h
	$(CC) $(CFLAGS) -c $(SRC)/server.c
client.o: $(SRC)/client.c $(SRC)/client.h
	$(CC) $(CFLAGS) -c $(SRC)/client.c

.PHONY: clean
clean:
	rm -f *.o server client
