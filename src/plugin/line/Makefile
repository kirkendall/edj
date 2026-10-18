LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/pluginline.so
SRC=	line.c syntax.c command.c
OBJ=	line.o syntax.o command.o
LIBS=	-lfineline
CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
CC=	gcc -g -fpic
#CC=	gcc -O -fpic

$(ALL):	pluginline.so
	cp pluginline.so $(PLUGIN)/pluginline.so

pluginline.so: $(OBJ)
	$(CC) $(LDFLAGS) -shared $(OBJ) $(LIBS) -o pluginline.so

clean:
	$(RM) $(PLUGIN)/pluginline.so
	$(RM) pluginline.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/line.html
