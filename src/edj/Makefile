LIB=	../../lib
INC=	../../include
BIN=	../../bin
HDR=	$(INC)/edj.h $(INC)/version.h
SRC=	edj.c interact.c batch.c commands.c complete.c
OBJ=	edj.o interact.o batch.o commands.o complete.o
CC=	gcc -g -fpic
#CC=	gcc -O -fpic
#CC=	gcc -O -pg -fpic
CFLAGS=	-Wall -I$(INC) -DEDJ_DEBUG_MEMORY
#CFLAGS=	-Wall -I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS= -ledj -lreadline
LDSTATLIBS=-ledjplugins -lm -ltinfo

all: $(BIN)/edj $(BIN)/edjstatic

$(OBJ): $(HDR)

$(BIN)/edj $(BIN)/edjstatic: edj edjstatic
	cp edj edjstatic $(BIN)

edj: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o edj

edjstatic: $(OBJ) $(LIB)/libedj.a $(LIB)/libedjplugins.a
	$(CC) -static $(LDFLAGS) $(OBJ) $(LDLIBS) $(LDSTATLIBS) -o edjstatic
	# strip edjstatic

clean:
	$(RM) edj
	$(RM) edjstatic
	$(RM) $(OBJ)
