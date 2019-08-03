FILES_1 = r_w_1.o
FILES_2 = r_w_2.o

all: ReadersAndWriters1 ReadersAndWriters2

ReadersAndWriters1: $(FILES_1)
	gcc $(FILES_1) -o ReadersAndWriters1 -pthread -std=c99 -lm -O3

ReadersAndWriters2: $(FILES_2)
	gcc $(FILES_2) -o ReadersAndWriters2 -pthread -std=c99 -lm -O3

$(FILES_1): r_w_1.c
$(FILES_2): r_w_1.c

.PHONY: clean

clean:
	rm -f *.o
	rm -f ReadersAndWriters1
	rm -f ReadersAndWriters2