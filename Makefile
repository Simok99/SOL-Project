CC = gcc
CFLAGS += -std=gnu99 -Wall -g
ARFLAGS = rvs
AR = ar
HDIR = ./headers
INCLUDE = -I. -I $(HDIR)
LDFLAG = -L.
CFILES = ./src/
EXPELLED_FILES = ./expelledFiles/
SCRIPT_DIR = ./scripts/
OBJFILES = ./objs/
LIB_DIR	= ./libs/
LIBS = -lpthread
TARGETS = server client

.PHONY: all clean cleanall

$(OBJFILES)%.o: $(CFILES)%.c
	@ mkdir -p objs
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

all:	$(TARGETS)

server: $(OBJFILES)server.o $(OBJFILES)util.o $(LIB_DIR)libPool.a $(LIB_DIR)libStruct.a
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^ $(LDFLAG) $(LIBS)

client: $(OBJFILES)client.o $(OBJFILES)util.o $(LIB_DIR)libStruct.a $(LIB_DIR)libApi.a
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $^

#Libreria threadpool
$(LIB_DIR)libPool.a: $(OBJFILES)threadpool.o
	@ mkdir -p libs
	$(AR) $(ARFLAGS) $@ $^

#Libreria strutture dati
$(LIB_DIR)libStruct.a: $(OBJFILES)hash.o $(OBJFILES)queue.o $(OBJFILES)fileList.o
	@ mkdir -p libs
	$(AR) $(ARFLAGS) $@ $^

#Libreria API
$(LIB_DIR)libApi.a: $(OBJFILES)api.o
	@ mkdir -p libs
	$(AR) $(ARFLAGS) $@ $^

$(OBJFILES)util.o: $(CFILES)util.c

$(OBJFILES)api.o: $(CFILES)api.c

$(OBJFILES)fileList.o: $(CFILES)fileList.c

$(OBJFILES)hash.o: $(CFILES)hash.c

$(OBJFILES)queue.o: $(CFILES)queue.c

$(OBJFILES)threadpool.o: $(CFILES)threadpool.c

$(OBJFILES)client.o: $(CFILES)client.c

$(OBJFILES)server.o: $(CFILES)server.c

#Script bash dei test
test1: client server
	$(SCRIPT_DIR)test1.sh
test2: client server
	$(SCRIPT_DIR)test2.sh
test3: client server
	$(SCRIPT_DIR)test3.sh
statistiche: client server
	$(SCRIPT_DIR)statistiche.sh

#Clean per eliminare targets, file oggetto e librerie
clean		: 
	rm -f $(TARGETS)
cleanall	: clean
	\rm -f *~ $(LIB_DIR)*.a $(OBJFILES)*.o $(SRC_DIR)*~
	\rm -rf $(OBJFILES)
	\rm -rf $(LIB_DIR)
	\rm -rf $(EXPELLED_FILES)