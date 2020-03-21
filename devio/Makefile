UNAME!=uname
UNAME?=$(shell uname)

all: devio.$(UNAME)

publish: /usr/utils/devio.bz2 /usr/utils/devio.lzma

install: /usr/local/bin/devio

devio.$(UNAME): devio.c ../inc/*.h safeio.c safeio.h Makefile
	gcc -Wall -Werror -Os -o devio.$(UNAME) devio.c safeio.c

devio.static.$(UNAME): devio.c ../inc/*.h safeio.c safeio.h Makefile
	gcc -Wall -Werror -static -Os -o devio.static.$(UNAME) devio.c safeio.c

/usr/utils/devio.bz2: devio.static.$(UNAME)
	bzip2 < devio.static.$(UNAME) > /usr/utils/devio.bz2

/usr/utils/devio.lzma: devio.static.$(UNAME)
	lzma e devio.static.$(UNAME) /usr/utils/devio.lzma

/usr/local/bin/devio: devio.$(UNAME)
	cp devio.$(UNAME) /usr/local/bin/devio
