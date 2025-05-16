CC=gcc
CFLAGS=-g -pedantic -std=gnu17 -Wall -Werror -Wextra -lpthread

.PHONY: all
all: nyuenc

nyuenc: nyuenc.c
	$(CC) $(CFLAGS) -o nyuenc nyuenc.c

.PHONY: clean
clean:
	rm -f nyuenc