UNAME_S!=uname
UNAME_S?=$(shell uname)

UNAME_M!=uname -m
UNAME_M?=$(shell uname -m)

UNAME=$(UNAME_S)_$(UNAME_M)

default: devio.$(UNAME)

static: devio.static.$(UNAME)

publish: /usr/utils/devio.gz /usr/utils/devio.xz

install: /usr/local/bin/devio

CC_OPT=-Wall -Werror -Os -D_XBS5_ILP32_OFFBIG

devio.$(UNAME): devio.c ../inc/*.h safeio.c safeio.h devio_types.h Makefile
	cc $(CC_OPT) -o devio.$(UNAME) devio.c safeio.c

devio.static.$(UNAME): devio.c ../inc/*.h safeio.c safeio.h devio_types.h Makefile
	cc $(CC_OPT) -static -o devio.static.$(UNAME) devio.c safeio.c

/usr/utils/devio.gz: devio.static.$(UNAME)
	gzip -9 < devio.static.$(UNAME) > /usr/utils/devio_$(UNAME).gz

/usr/utils/devio.xz: devio.static.$(UNAME)
	xz < devio.static.$(UNAME) > /usr/utils/devio_$(UNAME).xz

/usr/local/bin/devio: devio.$(UNAME)
	install devio.$(UNAME) /usr/local/bin/devio
