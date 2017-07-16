CFLAGS += -g -Wall -Werror
LDFLAGS += -pthread -lz -lbz2

pigdo: pigdo.o jigdo.o jigdo-template.o jigdo-md5.o decompress.o

.PHONY: clean docs

clean:
	$(RM) *.o pigdo
	$(RM) -r html latex

docs:
	doxygen
