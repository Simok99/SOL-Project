CC = gcc
CFLAGS += -Wall -g
HDIR = ./headers
INCLUDE = -I. -I $(HDIR)
CFILES = ./src/
OBJFILES = ./objs/
TARGETS = server

.PHONY: all clean

$(OBJFILES)%.o: $(CFILES)%.c
	@ mkdir -p objs
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

all:	$(TARGETS)

server: $(OBJFILES)server.o $(OBJFILES)util.o
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^

$(OBJFILES)server.o: $(CFILES)server.c

$(OBJFILES)util.o: $(CFILES)util.c