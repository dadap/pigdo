CFLAGS += -g -Wall -Werror

pigdo: pigdo.o jigdo.o jigdo-template.o jigdo-md5.o

.PHONY: clean

clean:
	$(RM) *.o pigdo
