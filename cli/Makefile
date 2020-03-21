INCLUDE=$(CRT_INC_PATH);$(INCLUDE)
LIB=$(SDK_LIB_DEST)\..\crt\i386;$(SDK_LIB_DEST)\i386;$(LIB)
CFLAGS=/nologo /WX /W4 /wd4201 /Ox /G7F
LFLAGS=/opt:ref,nowin98,icf=10 /nologo

imdisk.exe: imdisk.obj imdisk.res
	link $(LFLAGS) imdisk.obj imdisk.res

imdisk.obj: imdisk.c ..\inc\*.h
