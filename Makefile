OBJ =  fox-core.o
OBJ += fox-thread.o
OBJ += fox-rw.o
OBJ += fox-stats.o
OBJ += fox-vblk.o
OBJ += fox-buf.o
OBJ += fox-output.o
CC = gcc
CFLAGS = -g -Wall
CFLAGSXX =
DEPS =
SLIB = -lpthread -ludev -fopenmp
LLNVM = /usr/local/lib/liblightnvm.a

all: fox

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

fox : $(OBJ)
	$(CC) $(CFLAGS) $(CFLAGSXX) $(OBJ) -o fox $(LLNVM) $(SLIB)

clean:
	rm -f *.o fox