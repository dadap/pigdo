CFLAGS += -g -Wall -Werror -I/usr/local/include
LDFLAGS += -pthread -lz -lbz2 -lcurl -L/usr/local/lib

pigdo: pigdo.o jigdo.o jigdo-template.o jigdo-md5.o decompress.o fetch.o \
       util.o md5.o

.PHONY: clean docs

clean:
	$(RM) *.o pigdo
	$(RM) -r html latex

docs:
	doxygen
