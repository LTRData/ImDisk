
  ImDisk Virtual Disk Driver for Windows NT/2000/XP/2003/Vista/7/8/8.1/10.

  This driver emulates harddisk partitions, floppy drives and CD/DVD-ROM
  drives from disk image files, in virtual memory or by redirecting I/O
  requests somewhere else, possibly to another machine, through a
  co-operating user-mode service, ImDskSvc.

  To install this driver, service and command line tool, right-click on the
  imdisk.inf file and select 'Install'. To uninstall, use the Add/Remove
  Programs applet in the Control Panel.

  You can get syntax help to the command line tool by typing just imdisk
  without parameters.

  I have tested this product under 32 bit versions of Windows NT 3.51, NT 4.0,
  2000, XP, Server 2003, Vista, 7, 8, 8.1 and 10 and x86-64 versions of XP,
  Server 2003, Vista, 7, 8, 8.1 and 10. Primary target are older versions and
  there are several known compatibility issues on modern version of Windows.
  Please see website for more details: http://ltr-data.se/opencode.html#ImDisk

  The install/uninstall routines do not work under NT 3.51. If you want to use
  this product under NT 3.51 you have to manually add registry entries needed
  by driver and service or use resource kit tools to add necessary settings.

    Copyright (c) 2005-2018 Olof Lagerkvist
    http://www.ltr-data.se      olof@ltr-data.se

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use,
    copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.

    This software contains some GNU GPL licensed code:
    - Parts related to floppy emulation based on VFD by Ken Kato.
      http://chitchat.at.infoseek.co.jp/vmware/vfd.html
    Copyright (C) Free Software Foundation, Inc.
    Read gpl.txt for the full GNU GPL license.

    This software may contain BSD licensed code:
    - Some code ported to NT from the FreeBSD md driver by Olof Lagerkvist.
      http://www.ltr-data.se
    Copyright (c) The FreeBSD Project.
    Copyright (c) The Regents of the University of California.

