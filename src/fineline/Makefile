LIB=	../../lib
INCLUDE=../../include
SRC=	char.c draw2.c edit.c fineline.c history.c tty.c
OBJ=	char.o draw2.o edit.o fineline.o history.o tty.o
CC=	gcc -g -fpic
CFLAGS=	-Wall -I$(INCLUDE)

all: libs tryfl

libs: $(LIB)/libfineline.so $(LIB)/libfineline.a

$(LIB)/libfineline.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o $(LIB)/libfineline.so

$(LIB)/libfineline.a: $(OBJ)
	$(RM) $(LIB)/libfineline.a
	ar q $(LIB)/libfineline.a $(OBJ)

tryfl: tryfl.c libs
	$(CC) tryfl.c -L$(LIB) -lfineline -o tryfl

try: tryfl
	LD_LIBRARY_PATH=$(LIB) ./tryfl || stty sane

tags: $(SRC) fineline.h
	elvtags $(SRC) fineline.h

clean:
	$(RM) $(OBJ)
	$(RM) $(LIB)/libfineline.so $(LIB)/libfineline.a
	$(RM) tags
