VERSION=0x000900
CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2 -DJSER_VERSION="${VERSION}"
TARGET=jser
DESTDIR=install

all: ${TARGET}

main.o: main.c ${TARGET}.h

${TARGET}.o: ${TARGET}.c ${TARGET}.h

lib${TARGET}.a: ${TARGET}.o ${TARGET}.h
	ar rcs $@ $<

${TARGET}: main.o lib${TARGET}.a

run: ${TARGET}
	./${TARGET} -e

test: ${TARGET}
	./${TARGET} -t

clean:
	rm -fv ${TARGET} *.a *.o
	#git clean -dfx
