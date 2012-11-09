CC=arm-linux-gnueabi-gcc
CFLAGS=-Wall -Wextra -I/usr/xenomai/include -I../libpsgc
LDFLAGS=-L/usr/xenomai/lib -L../libpsgc -lxenomai -lnative -lpsgc
OBJ=main.o speed.o gps.o
BIN=ecollect

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(BIN)
	rm -f $(OBJ)

.PHONY: clean
