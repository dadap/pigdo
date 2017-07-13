CFLAGS += -g

pigdo: pigdo.o jigdo.o

.PHONY: clean

clean:
	$(RM) *.o pigdo
