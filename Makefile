!IF "$(_BUILDARCH)" == "x86"
ARCHDIR=i386
!ELSEIF "$(_BUILDARCH)" == "AMD64"
ARCHDIR=amd64
!ELSEIF "$(_BUILDARCH)" == "IA64"
ARCHDIR=ia64
!ENDIF

BUILD_DEFAULT=-cegiw -nmake -i

all: cli\$(ARCHDIR)\imdisk.exe svc\$(ARCHDIR)\imdsksvc.exe cpl\$(ARCHDIR)\imdisk.cpl sys\$(ARCHDIR)\imdisk.sys awealloc\$(ARCHDIR)\awealloc.sys deviotst\$(ARCHDIR)\deviotst.exe

clean:
	del /s *~ *.obj *.log *.wrn *.err *.mac *.o

publish: p:\utils\imdiskinst.exe p:\utils\imdisk_source.7z
	start explorer ftp://ftp.ltr-data.se/ltr-data.se/public_html/files
	start z:\ltr-website\files\

p:\utils\imdiskinst.exe: p:\utils\imdisk.7z p:\utils\7zSD.sfx 7zSDcfg.txt
	copy /y /b p:\utils\7zSD.sfx + 7zSDcfg.txt + p:\utils\imdisk.7z p:\utils\imdiskinst.exe
	signtool sign /v /n "Lagerkvist Teknisk Radgivning i Boras HB" /d "ImDisk Virtual Disk Driver" /du "http://www.ltr-data.se" /ac Z:\Kod\cert\MSCV-GlobalSign.cer /t "http://timestamp.verisign.com/scripts/timestamp.dll" p:\utils\imdiskinst.exe
	xcopy /d /y p:\utils\imdiskinst.exe z:\ltr-website\files\

p:\utils\imdisk_source.7z: p:\utils\imdisk.7z 7zSDcfg.txt gpl.txt readme.txt runwait.exe install.cmd msgbox.exe devio\*.c devio\*.cpp devio\*.h devio\Makefile* uninstall.cmd ImDiskNet\*.sln ImDiskNet\ImDiskNet\*.vb ImDiskNet\ImDiskNet\*.snk ImDiskNet\ImDiskNet\*.vbproj Makefile
	del p:\utils\imdisk_source.7z
	7z a -r p:\utils\imdisk_source.7z -x!*~ -m0=PPMd 7zSDcfg.txt gpl.txt readme.txt *.def *.src *.ico *.c *.h *.cpp *.hpp *.cxx *.hxx *.rc *.lib *.sln *.vb *.cs *.*proj *.snk *.resx *.resources *.myapp *.settings Sources dirs imdisk.inf runwait.exe install.cmd msgbox.exe uninstall.cmd Makefile*
	xcopy /d /y p:\utils\imdisk_source.7z z:\ltr-website\files\

p:\utils\imdisk.7z: readme.txt gpl.txt imdisk.inf runwait.exe install.cmd msgbox.exe cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys awealloc\i386\awealloc.sys cli\ia64\imdisk.exe cpl\ia64\imdisk.cpl svc\ia64\imdsksvc.exe sys\ia64\imdisk.sys awealloc\ia64\awealloc.sys cli\amd64\imdisk.exe cpl\amd64\imdisk.cpl svc\amd64\imdsksvc.exe sys\amd64\imdisk.sys awealloc\amd64\awealloc.sys
	del p:\utils\imdisk.7z
	7z a p:\utils\imdisk.7z -m0=LZMA:a=2 readme.txt gpl.txt imdisk.inf runwait.exe install.cmd msgbox.exe cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys awealloc\i386\awealloc.sys cli\ia64\imdisk.exe cpl\ia64\imdisk.cpl svc\ia64\imdsksvc.exe sys\ia64\imdisk.sys awealloc\ia64\awealloc.sys cli\amd64\imdisk.exe cpl\amd64\imdisk.cpl svc\amd64\imdsksvc.exe sys\amd64\imdisk.sys awealloc\amd64\awealloc.sys

cli\$(ARCHDIR)\imdisk.exe: cli\sources cli\*.c cli\*.rc inc\*.h cpl\$(ARCHDIR)\imdisk.lib
	cd cli
	build
	cd $(MAKEDIR)

cpl\$(ARCHDIR)\imdisk.cpl cpl\$(ARCHDIR)\imdisk.lib: cpl\sources cpl\*.c cpl\*.cpp cpl\*.rc cpl\*.src cpl\*.ico cpl\*.h inc\*.h
	cd cpl
	build
	cd $(MAKEDIR)

svc\$(ARCHDIR)\imdsksvc.exe: svc\sources svc\*.cpp svc\*.rc inc\*.h inc\*.hpp
	cd svc
	build
	cd $(MAKEDIR)

sys\$(ARCHDIR)\imdisk.sys: sys\sources sys\*.c sys\*.rc inc\*.h
	cd sys
	build
	cd $(MAKEDIR)
#!IF "$(_BUILDARCH)" != "x86"
#	signtool sign /v /s PrivateCertStore /n ltr-data.se(Test) /d "ImDisk Virtual Disk Driver" /du "http://www.ltr-data.se" /t "http://timestamp.verisign.com/scripts/timestamp.dll" sys\$(ARCHDIR)\imdisk.sys
	signtool sign /v /n "Lagerkvist Teknisk Radgivning i Boras HB" /d "ImDisk Virtual Disk Driver" /du "http://www.ltr-data.se" /ac Z:\Kod\cert\MSCV-GlobalSign.cer /t "http://timestamp.verisign.com/scripts/timestamp.dll" sys\$(ARCHDIR)\imdisk.sys
#!ENDIF

awealloc\$(ARCHDIR)\awealloc.sys: awealloc\sources awealloc\*.c awealloc\*.rc
	cd awealloc
	build
	cd $(MAKEDIR)
#!IF "$(_BUILDARCH)" != "x86"
#	signtool sign /v /s PrivateCertStore /n ltr-data.se(Test) /d "AWE Allocation Driver" /du "http://www.ltr-data.se" /t "http://timestamp.verisign.com/scripts/timestamp.dll" awealloc\$(ARCHDIR)\awealloc.sys
	signtool sign /v /n "Lagerkvist Teknisk Radgivning i Boras HB" /d "AWE Allocation Driver" /du "http://www.ltr-data.se" /ac Z:\Kod\cert\MSCV-GlobalSign.cer /t "http://timestamp.verisign.com/scripts/timestamp.dll" awealloc\$(ARCHDIR)\awealloc.sys
#!ENDIF

deviotst\$(ARCHDIR)\deviotst.exe: deviotst\sources deviotst\deviotst.cpp inc\*.h inc\*.hpp
	cd deviotst
	build
	cd $(MAKEDIR)
