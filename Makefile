all: nanows

bin:
	mkdir -p bin

nanows: bin server.c
	$(CC) -Wall -O4 -o bin/nanows server.c
