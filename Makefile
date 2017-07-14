CFLAGS += -g -Wall -Werror

pigdo: pigdo.o jigdo.o

.PHONY: clean

clean:
	$(RM) *.o pigdo
