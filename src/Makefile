all:
	make -e -C lib
	for i in */.; do [ $$i = lib/. ] || make -e -C $$i; done

clean:
	for i in */.; do make -e -C $$i clean; done
