.PHONY: all clean
all:
	gcc -Wno-int-conversion -o code main.c buddy.c

clean:
	rm -f code test *.o