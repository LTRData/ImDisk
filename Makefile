!IF EXIST(Makefile.user)
!INCLUDE Makefile.user
!ENDIF

!IFNDEF TIMESTAMP_WEBSERVICE
TIMESTAMP_WEBSERVICE=http://timestamp.comodoca.com/authenticode
#TIMESTAMP_WEBSERVICE=http://timestamp.verisign.com/scripts/timestamp.dll
!ENDIF

!IFNDEF ARCHDIR
!IF "$(_BUILDARCH)" == "x86"
ARCHDIR=i386
!ELSEIF "$(_BUILDARCH)" == "AMD64"
ARCHDIR=amd64
!ELSEIF "$(_BUILDARCH)" == "IA64"
ARCHDIR=ia64
!ELSE
!ERROR _BUILDARCH not defined. Please set environment variables for WDK 7.x or earlier build environments.
!ENDIF
!ENDIF

!IFNDEF SIGNTOOL
# To make signtool work with this Makefile, define:
# SIGNTOOL=signtool sign /a /v
# COMPANYNAME=Company name in your certificate
# COMPANYURL=http://www.ltr-data.se or your company website
# CERTPATH=Path to cross-sign certificate, or self-signed test cert
# To build without signing, leave following line defined instead
SIGNTOOL=@rem
!ENDIF

BUILD_DEFAULT=-cegiw -nmake -i

INCLUDE=$(INCLUDE);$(MAKEDIR)\inc

README_TXT_FILES=gpl.txt readme.txt

!IFNDEF DIST_DIR
DIST_DIR=$(MAKEDIR)\dist
!ENDIF

!IFNDEF UPLOAD_DIR
UPLOAD_DIR=$(MAKEDIR)\dist\setup
!ENDIF

STAMPINF_VERSION=$(IMDISK_VERSION)

all: cli\$(ARCHDIR)\imdisk.exe svc\$(ARCHDIR)\imdsksvc.exe cpl\$(ARCHDIR)\imdisk.cpl cplcore\$(ARCHDIR)\imdisk.cpl sys\$(ARCHDIR)\imdisk.sys awealloc\$(ARCHDIR)\awealloc.sys deviotst\$(ARCHDIR)\deviotst.exe

clean:
	del /s *~ *.obj *.log *.wrn *.err *.mac *.o

publish: $(DIST_DIR) $(UPLOAD_DIR) $(DIST_DIR)\imdiskinst.exe $(DIST_DIR)\imdisk_source.7z
	start $(UPLOAD_DIR)

$(DIST_DIR) $(UPLOAD_DIR):
	mkdir $@

$(DIST_DIR)\imdiskinst.exe: $(DIST_DIR)\imdisk.7z 7zSD.sfx 7zSDcfg.txt
	copy /y /b 7zSD.sfx + 7zSDcfg.txt + $(DIST_DIR)\imdisk.7z $(DIST_DIR)\imdiskinst.exe
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "ImDisk Virtual Disk Driver" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $(DIST_DIR)\imdiskinst.exe
	xcopy /d /y $(DIST_DIR)\imdiskinst.exe $(UPLOAD_DIR)

$(DIST_DIR)\imdisk_source.7z: $(DIST_DIR)\imdisk.7z 7zSDcfg.txt $(README_TXT_FILES) runwaitw.exe install.cmd msgboxw.exe inc\imdiskver.h devio\*.c devio\*.cpp devio\*.h devio\Makefile* uninstall_imdisk.cmd 7zSD.sfx *.sln *.props ImDiskNet\*.sln ImDiskNet\ImDiskNet\*.vb ImDiskNet\ImDiskNet\*.*proj ImDiskNet\DiscUtilsDevio\*.vb ImDiskNet\DiscUtilsDevio\*.*proj ImDiskNet\DevioNet\*.vb ImDiskNet\DevioNet\*.*proj Makefile
	del $(DIST_DIR)\imdisk_source.7z
	7z a -r $(DIST_DIR)\imdisk_source.7z -x!*~ -m0=PPMd 7zSDcfg.txt 7zSD.sfx $(README_TXT_FILES) *.def *.src *.ico *.c *.h *.cpp *.hpp *.cxx *.hxx *.rc *.lib *.sln *.vb *.cs *.*proj *.snk *.resx *.resources *.myapp *.settings *.props Sources dirs imdisk.inf runwaitw.exe install.cmd msgboxw.exe uninstall_imdisk.cmd Makefile*
	xcopy /d /y $(DIST_DIR)\imdisk_source.7z $(UPLOAD_DIR)

$(DIST_DIR)\imdisk.7z: $(README_TXT_FILES) imdisk.inf runwaitw.exe install.cmd uninstall_imdisk.cmd msgboxw.exe cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys awealloc\i386\awealloc.sys cli\ia64\imdisk.exe cpl\ia64\imdisk.cpl svc\ia64\imdsksvc.exe sys\ia64\imdisk.sys awealloc\ia64\awealloc.sys cli\amd64\imdisk.exe cpl\amd64\imdisk.cpl svc\amd64\imdsksvc.exe sys\amd64\imdisk.sys awealloc\amd64\awealloc.sys
	del $(DIST_DIR)\imdisk.7z
	stampinf -f imdisk.inf -a NTx86,NTia64,NTamd64
	7z a $(DIST_DIR)\imdisk.7z -m0=LZMA:a=2 $(README_TXT_FILES) imdisk.inf runwaitw.exe install.cmd uninstall_imdisk.cmd msgboxw.exe cli\i386\imdisk.exe cpl\i386\imdisk.cpl svc\i386\imdsksvc.exe sys\i386\imdisk.sys awealloc\i386\awealloc.sys cli\ia64\imdisk.exe cpl\ia64\imdisk.cpl svc\ia64\imdsksvc.exe sys\ia64\imdisk.sys awealloc\ia64\awealloc.sys cli\amd64\imdisk.exe cpl\amd64\imdisk.cpl svc\amd64\imdsksvc.exe sys\amd64\imdisk.sys awealloc\amd64\awealloc.sys

cli\$(ARCHDIR)\imdisk.exe: cli\sources cli\*.c cli\*.rc inc\*.h cpl\$(ARCHDIR)\imdisk.lib cplcore\$(ARCHDIR)\imdisk.lib
	cd cli
	build
	cd $(MAKEDIR)
	editbin /nologo /subsystem:console,4.00 $@
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "ImDisk Virtual Disk Driver Command line tool" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $@

cpl\$(ARCHDIR)\imdisk.lib: cpl\$(ARCHDIR)\imdisk.cpl

cpl\$(ARCHDIR)\imdisk.cpl: cpl\sources cpl\*.c cpl\*.cpp cpl\*.rc cpl\*.src cpl\*.ico cpl\*.h inc\*.h
	cd cpl
	build
	cd $(MAKEDIR)
	editbin /nologo /subsystem:windows,4.00 $@
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "ImDisk Virtual Disk Driver Control Panel Applet" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $@

cplcore\$(ARCHDIR)\imdisk.lib: cplcore\$(ARCHDIR)\imdisk.cpl

cplcore\$(ARCHDIR)\imdisk.cpl: cplcore\sources cplcore\*.c cpl\*.c cpl\*.cpp cpl\*.rc cplcore\*.src cplcore\*.h cpl\*.h inc\*.h
	cd cplcore
	nmake refresh
	build
	cd $(MAKEDIR)
	editbin /nologo /subsystem:windows,4.00 $@
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "ImDisk Virtual Disk Driver Core API Library" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $@

svc\$(ARCHDIR)\imdsksvc.exe: svc\sources svc\*.cpp svc\*.rc inc\*.h inc\*.hpp
	cd svc
	build
	cd $(MAKEDIR)
	editbin /nologo /subsystem:console,4.00 $@
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "ImDisk Virtual Disk Driver Helper Service" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $@

sys\$(ARCHDIR)\imdisk.sys: sys\sources sys\*.cpp sys\*.rc inc\*.h
	cd sys
	build
	cd $(MAKEDIR)
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "ImDisk Virtual Disk Driver" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $@

awealloc\$(ARCHDIR)\awealloc.sys: awealloc\sources awealloc\*.c awealloc\*.rc inc\*.h
	cd awealloc
	build
	cd $(MAKEDIR)
	$(SIGNTOOL) /n "$(COMPANYNAME)" /d "AWE Allocation Driver" /du "$(COMPANYURL)" /ac $(CERTPATH) /t "$(TIMESTAMP_WEBSERVICE)" $@

deviotst\$(ARCHDIR)\deviotst.exe: deviotst\sources deviotst\deviotst.cpp inc\*.h inc\*.hpp
	cd deviotst
	build
	cd $(MAKEDIR)
	editbin /nologo /subsystem:console,4.00 $@
