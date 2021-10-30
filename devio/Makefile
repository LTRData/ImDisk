UNAME_S!=uname
UNAME_S?=$(shell uname)

UNAME_M!=uname -m
UNAME_M?=$(shell uname -m)

UNAME=$(UNAME_S)_$(UNAME_M)

DIST=../dist

default: devio.$(UNAME)

static: devio.static.$(UNAME)

publish: $(DIST)/devio_$(UNAME).gz $(DIST)/devio_$(UNAME).xz $(DIST)/devio_$(UNAME).bz2 $(DIST)/devio_static_$(UNAME).gz $(DIST)/devio_static_$(UNAME).xz $(DIST)/devio_static_$(UNAME).bz2

install: /usr/local/bin/devio

CC_OPT=-Wall -Werror -Os -D_XBS5_ILP32_OFFBIG

devio.$(UNAME): devio.c ../inc/*.h safeio.c safeio.h devio_types.h Makefile
	cc $(CC_OPT) -o devio.$(UNAME) devio.c safeio.c

devio.static.$(UNAME): devio.c ../inc/*.h safeio.c safeio.h devio_types.h Makefile
	cc $(CC_OPT) -static -o devio.static.$(UNAME) devio.c safeio.c

$(DIST)/devio_$(UNAME).gz: devio.static.$(UNAME)
	gzip -9 < devio.$(UNAME) > $(DIST)/devio_$(UNAME).gz

$(DIST)/devio_$(UNAME).xz: devio.static.$(UNAME)
	xz < devio.$(UNAME) > $(DIST)/devio_$(UNAME).xz

$(DIST)/devio_$(UNAME).bz2: devio.static.$(UNAME)
	bzip2 < devio.$(UNAME) > $(DIST)/devio_$(UNAME).bz2

$(DIST)/devio_static_$(UNAME).gz: devio.static.$(UNAME)
	gzip -9 < devio.static.$(UNAME) > $(DIST)/devio_static_$(UNAME).gz

$(DIST)/devio_static_$(UNAME).xz: devio.static.$(UNAME)
	xz < devio.static.$(UNAME) > $(DIST)/devio_static_$(UNAME).xz

$(DIST)/devio_static_$(UNAME).bz2: devio.static.$(UNAME)
	bzip2 < devio.static.$(UNAME) > $(DIST)/devio_static_$(UNAME).bz2

/usr/local/bin/devio: devio.$(UNAME)
	install devio.$(UNAME) /usr/local/bin/devio
