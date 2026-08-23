LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincsv.so
SRC=	csv.c
OBJ=	csv.o
LIBS=	
CFLAGS=	-I$(INC) -DEDJ_DEBUG_MEMORY
#CC=	gcc -g -fpic
CC=	gcc -O -fpic

$(ALL): plugincsv.so
	cp plugincsv.so $(PLUGIN)/plugincsv.so

plugincsv.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincsv.so

clean:
	$(RM) $(PLUGIN)/plugincsv.so
	$(RM) plugincsv.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/csv.html
