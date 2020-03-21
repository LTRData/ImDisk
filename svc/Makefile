INCLUDE=$(CRT_INC_PATH);$(INCLUDE)
LIB=$(DDK_LIB_DEST)\i386;$(LIB)
CFLAGS=/nologo /WX /W4 /wd4201 /Ox /G7F
CPPFLAGS=$(CFLAGS)
LFLAGS=/opt:ref,nowin98,icf=10 /release /nologo

imdsksvc.exe: imdsksvc.obj imdsksvc.res Makefile
	link $(LFLAGS) imdsksvc.obj imdsksvc.res

imdsksvc.obj: imdsksvc.cpp ..\inc\*.hpp ..\inc\*.h
