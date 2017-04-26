OBJ =  fox-core.o
OBJ += fox-thread.o
OBJ += fox-rw.o
OBJ += fox-stats.o
OBJ += fox-vblk.o
OBJ += fox-buf.o
OBJ += fox-output.o
OBJ += fox-argp.o
OBJ += engines/fox-sequential.o
OBJ += engines/fox-round-robin.o
OBJ += engines/fox-isolation.o
CC = gcc
CFLAGS = -O2 -Wall
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
	rm -f *.o engines/*.o fox