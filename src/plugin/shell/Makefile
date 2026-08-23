LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/pluginshell.so
SRC=	shell.c
OBJ=	shell.o
LIBS=	
CFLAGS=	-I$(INC)
#CC=	gcc -fpic -g
CC=	gcc -fpic -O

$(ALL): pluginshell.so
	cp pluginshell.so $(PLUGIN)/pluginshell.so

pluginshell.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o pluginshell.so

clean:
	$(RM) $(PLUGIN)/pluginshell.so
	$(RM) pluginshell.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/shell.html
	$(RM) $(WWW)/f/shell/*.html
	$(RM) $(WWW)/cmd/shell/*.html
