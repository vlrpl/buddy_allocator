CC=gcc
CFLAGS += -g -m32


all: buddy
	$(CC) -m32 -o buddy alloc_pages.o

buddy: alloc_pages.o

clean:
	rm -f ./alloc_pages.o buddy
	

.PHONY: clean
