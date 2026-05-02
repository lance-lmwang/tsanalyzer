CC = gcc
CFLAGS = -Iinclude -Isrc/core -O0 -g -Wall -fPIC -fno-omit-frame-pointer
LDFLAGS = -shared

SRC = src/core/tsshaper.c src/core/tss_scheduler.c src/core/tss_fifo.c src/core/tss_metrics.c
OBJ = $(SRC:.c=.o)
LIB_SO = libtsshaper.so
LIB_A  = libtsshaper.a

all: $(LIB_SO) $(LIB_A)

$(LIB_SO): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(LIB_A): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(LIB_SO) $(LIB_A)

install: $(LIB)
	cp $(LIB) /usr/local/lib/
	cp -r include/tsshaper /usr/local/include/
