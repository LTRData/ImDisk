BUILD_OPTIONS=-cegiw -nmake -i

all: cli\i386\imdisk.exe svc\i386\imdsksvc.exe cpl\i386\imdisk.cpl sys\i386\imdisk.sys deviotst\i386\deviotst.exe

clean:
	del /s *~ *.obj *.res *.log *.wrn *.err *.mac *.o

publish: p:\utils\imdiskinst.exe p:\utils\imdisk_source.7z

p:\utils\imdiskinst.exe: p:\utils\imdisk.7z p:\utils\7zSD.sfx 7zSDcfg.txt
	copy /y /b p:\utils\7zSD.sfx + 7zSDcfg.txt + p:\utils\imdisk.7z p:\utils\imdiskinst.exe
	copy /y /b p:\utils\7zSD.sfx + 7zSDcfg.txt + p:\utils\imdisk.7z z:\ltr-website\files\imdiskinst.exe
	start explorer ftp://ftp.ltr-data.se/ltr-data.se/public_html/files
	start z:\ltr-website\files\

p:\utils\imdisk.7z: readme.txt gpl.txt cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys imdisk.inf runwait.exe
	del p:\utils\imdisk.7z
	7z a p:\utils\imdisk.7z -m0=LZMA:a=2 readme.txt gpl.txt cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys imdisk.inf runwait.exe

p:\utils\imdisk_source.7z: *.txt cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys deviotst\i386\deviotst.exe imdisk.inf runwait.exe uninstall.cmd Makefile
	del p:\utils\imdisk_source.7z
	7z a -r p:\utils\imdisk_source.7z -m0=PPMd *.txt *.def *.ico *.c *.h *.cpp *.hpp *.rc *.lib Sources dirs imdisk.inf runwait.exe uninstall.cmd Makefile
	xcopy p:\utils\imdisk_source.7z z:\ltr-website\files\ /d/y

runwait.exe: p:\utils\runwait.exe
	copy /y p:\utils\runwait.exe runwait.exe

cli\i386\imdisk.exe: cli\sources cli\*.c cli\*.rc inc\*.h cpl\i386\imdisk.lib
	cd cli
	build
	cd $(MAKEDIR)

cpl\i386\imdisk.cpl cpl\i386\imdisk.lib: cpl\sources cpl\*.c cpl\*.cpp cpl\*.rc cpl\*.def cpl\*.ico cpl\*.h inc\*.h
	cd cpl
	build
	cd $(MAKEDIR)

svc\i386\imdsksvc.exe: svc\sources svc\*.cpp svc\*.rc inc\*.h inc\*.hpp
	cd svc
	build
	cd $(MAKEDIR)

sys\i386\imdisk.sys: sys\sources sys\*.c sys\*.rc inc\*.h
	cd sys
	build
	cd $(MAKEDIR)

deviotst\i386\deviotst.exe: deviotst\sources deviotst\deviotst.cpp inc\*.h inc\*.hpp
	cd deviotst
	build
	cd $(MAKEDIR)
