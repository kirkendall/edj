LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/pluginreadline.so
SRC=	readline.c complete.c
OBJ=	readline.o complete.o
LIBS=	-lreadline
CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
CC=	gcc -g -fpic
#CC=	gcc -O -fpic

$(ALL):	pluginreadline.so
	cp pluginreadline.so $(PLUGIN)/pluginreadline.so

pluginreadline.so: $(OBJ)
	$(CC) $(LDFLAGS) -shared $(OBJ) $(LIBS) -o pluginreadline.so

clean:
	$(RM) $(PLUGIN)/pluginreadline.so
	$(RM) pluginreadline.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/readline.html
