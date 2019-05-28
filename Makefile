all: uevent.parser.c uevent.scanner.c
	$(CC) -g -O0 -D_GNU_SOURCE -Wall -Wextra -o uevent uevent.parser.c uevent.scanner.c uevent.atom.c

clean:
	@rm -f -- uevent uevent.parser.h uevent.parser.c uevent.scanner.h uevent.scanner.c

uevent.parser.h: uevent.parser.c
uevent.parser.c: uevent.parser.y
	bison -d --defines=uevent.parser.h uevent.parser.y

uevent.scanner.h: uevent.scanner.c
uevent.scanner.c: uevent.scanner.l uevent.parser.h
	flex uevent.scanner.l
