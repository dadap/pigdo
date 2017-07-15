CFLAGS += -g -Wall -Werror

pigdo: pigdo.o jigdo.o jigdo-template.o jigdo-md5.o

.PHONY: clean docs

clean:
	$(RM) *.o pigdo
	$(RM) -r html latex

docs:
	doxygen
