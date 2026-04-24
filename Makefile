CC = gcc
CFLAGS = -Wall -Wextra -pthread

all: servidor

servidor: servidor.o claves.o
	$(CC) $(CFLAGS) -o servidor servidor.o claves.o

servidor.o: servidor.c claves.h
	$(CC) $(CFLAGS) -c servidor.c

claves.o: claves.c claves.h
	$(CC) $(CFLAGS) -c claves.c

clean:
	rm -f *.o servidor