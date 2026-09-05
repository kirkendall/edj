LIB=	../../lib
INCLUDE=../../include
HDRS=	$(INCLUDE)/edj.h $(INCLUDE)/version.h
LIBS=	-ldl
LIBSRC=	by.c blob.c calc.c calcfunc.c calcparse.c common.c compare.c config.c \
	context.c copy.c cmd.c datetime.c debug.c defer.c diff.c equal.c \
	explain.c file.c find.c flat.c format.c grid.c is.c length.c mbstr.c \
	memory.c parse.c plugin.c print.c serialize.c sort.c text.c user.c \
	walk.c
LIBOBJ=	by.o blob.o calc.o calcfunc.o calcparse.o common.o compare.o config.o \
	context.o copy.o cmd.o datetime.o debug.o defer.o diff.o equal.o \
	explain.o file.o find.o flat.o format.o grid.o is.o length.o mbstr.o \
	memory.o parse.o print.o serialize.o sort.o text.o user.o walk.o
#STATIC= -DSTATICCACHE -DSTATICCSV -DSTATICCURL -DSTATICLOG -DSTATICMATH -DSTATICXML
STATIC=	-DSTATICCACHE -DSTATICCSV -DSTATICLOG -DSTATICMATH -DSTATICXML
#CC=gcc -g -pg -fpic -Wall
#CC=gcc -O -fpic -Wall
CC=gcc -g -fpic -Wall
CFLAGS=-Wall -DEDJ_DEBUG_MEMORY -I$(INCLUDE)
#CFLAGS=-Wall -I$(INCLUDE)

all: $(LIB)/libedj.so $(LIB)/libedj.a

$(LIBOBJ): $(HDRS)

$(LIB)/libedj.so: $(LIBOBJ) plugin.o
	$(CC) $(LIBOBJ) plugin.o $(LIBS) -shared -o $(LIB)/libedj.so

$(LIB)/libedj.a: $(LIBOBJ) pluginstatic.o
	$(RM) $(LIB)/libedj.a
	ar q $(LIB)/libedj.a $(LIBOBJ) pluginstatic.o

pluginstatic.o: plugin.c
	$(CC) $(CFLAGS) $(STATIC) -c plugin.c -o pluginstatic.o

clean:
	$(RM) $(LIBOBJ)
	$(RM) plugin.o
	$(RM) pluginstatic.o
	$(RM) $(LIB)/libedj.so
	$(RM) $(LIB)/libedj.a

tags: $(LIBSRC)
	ctags $(LIBSRC)

