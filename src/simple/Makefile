LIB=	../../lib
INC=	../../include
CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ledj

all: simple

simple: simple.c $(LIB)/libedj.so
	$(CC) $(CFLAGS) $(LDFLAGS) simple.c $(LDLIBS) -o simple

clean:
	$(RM) simple
