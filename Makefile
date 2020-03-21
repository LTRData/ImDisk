all: cli\i386\imdisk.exe svc\i386\imdsksvc.exe cpl\i386\imdisk.cpl sys\i386\imdisk.sys deviotst\i386\deviotst.exe

clean:
	del /s *~ *.obj *.res *.log *.wrn *.err *.mac *.o

install: p:\utils\imdiskinst.exe p:\utils\imdisk_source.7z

p:\utils\imdiskinst.exe: p:\utils\imdisk.7z p:\utils\7zSD.sfx 7zSDcfg.txt p:\utils\imdisk_source.7z
	del p:\utils\imdisk.7z
	7z a    p:\utils\imdisk.7z -m0=LZMA:a=2 *.txt cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys imdisk.inf install.cmd
	copy /y /b p:\utils\7zSD.sfx + 7zSDcfg.txt + p:\utils\imdisk.7z p:\utils\imdiskinst.exe
	copy /y /b p:\utils\7zSD.sfx + 7zSDcfg.txt + p:\utils\imdisk.7z z:\ltr-website\files\imdiskinst.exe
	xcopy /d/y p:\utils\imdisk*.7z \\viared\gemensambjg\data\kod\imdisk\ 
	start explorer ftp://ftp.ltr-data.se/ltr-data.se/public_html/files
	start z:\ltr-website\files\

p:\utils\imdisk_source.7z: *.txt cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys deviotst\i386\deviotst.exe imdisk.inf install.cmd uninstall.cmd Makefile
	del p:\utils\imdisk_source.7z
	7z a -r p:\utils\imdisk_source.7z -m0=PPMd *.txt *.c *.h *.cpp *.hpp *.rc *.lib Sources imdisk.inf install.cmd uninstall.cmd Makefile
	xcopy p:\utils\imdisk_source.7z z:\ltr-website\files\ /d/y

cli\i386\imdisk.exe: cli\sources cli\*.c cli\*.rc inc\*.h
	cd cli
	build -c
	cd $(MAKEDIR)

cpl\i386\imdisk.cpl: cpl\sources cpl\*.c cpl\*.cpp cpl\*.rc cpl\*.h inc\*.h
	cd cpl
	build -c
	cd $(MAKEDIR)

svc\i386\imdsksvc.exe: svc\sources svc\*.cpp svc\*.rc inc\*.h inc\*.hpp
	cd svc
	build -c
	cd $(MAKEDIR)

sys\i386\imdisk.sys: sys\sources sys\*.c sys\*.rc inc\*.h
	cd sys
	build -c
	cd $(MAKEDIR)

deviotst\i386\deviotst.exe: deviotst\sources deviotst\deviotst.cpp inc\*.h inc\*.hpp
	cd deviotst
	build -c
	cd $(MAKEDIR)
