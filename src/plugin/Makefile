LIB=	../../lib

all:
	for i in */.; do make -C $$i -e; done
	$(RM) $(LIB)/edjplugins.a
	ar q $(LIB)/libedjplugins.a */*.o 

clean:
	for i in */.; do make -C $$i -e clean; done
	$(RM) $(LIB)/edjplugins.a
