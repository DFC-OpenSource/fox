OBJ = fox-core.o fox-thread.o fox-rw.o fox-stats.o fox-vblk.o fox-buf.o
CC = gcc
CFLAGS = -g
CFLAGSXX =
DEPS =

all: fox

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

fox : $(OBJ)
	$(CC) $(CFLAGS) $(CFLAGSXX) $(OBJ) -o fox /usr/local/lib/liblightnvm.a -lpthread -ludev -fopenmp

clean:
	rm -f *.o fox
