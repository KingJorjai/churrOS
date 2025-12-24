CFLAGS = -Wall -ggdb

SRC = *.c
HEAD = *.h

prometheus: $(SRC) $(HEAD)
	gcc -o $@ $(SRC) $(CFLAGS)  

.PHONY: clean
clean:
	rm -f *.o prometheus

