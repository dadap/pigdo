CFLAGS += -g -Wall -Werror
LDFLAGS += -pthread -lz -lbz2 -lcurl

pigdo: pigdo.o jigdo.o jigdo-template.o jigdo-md5.o decompress.o fetch.o util.o

.PHONY: clean docs

clean:
	$(RM) *.o pigdo
	$(RM) -r html latex

docs:
	doxygen
