CC=gcc

CFLAGS+= -g -D_GNU_SOURCE -fno-stack-protector -fPIC 

LDFLAGS+=

OBJS= libaga.o agalog.o

TARGETS= libaga.so

.c.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: $(OBJS) $(TARGETS)

libaga.o: libaga.h
agalog.o: agalog.h

libaga.so: $(OBJS)
	$(CC) $(CLFAGS) -shared $(OBJS) -o libaga.so

clean: forcelook
	rm -f $(OBJS) *.o $(TARGETS)

forcelook:
	true
