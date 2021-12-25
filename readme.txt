
  ImDisk Virtual Disk Driver for Windows NT/2000/XP/2003/Vista/7/8/8.1/10

  PLEASE NOTE: This project is not recommended on recent versions of
  Windows (versions after XP). There are many known compatibility
  issues because of a very old design for compatibility with as old
  versions as Windows NT 3.51.  No new features will be added to this
  project but it will remain available here because it could still
  be useful in certain scenarios.

  I will continue development of Arsenal Image Mounter instead. That
  has a different design and emulates complete disks and is compatible
  with most cases where physical disk are normally used.
  https://github.com/ArsenalRecon/Arsenal-Image-Mounter

  Back to this project, ImDisk Virtual Disk driver.
  This driver emulates harddisk partitions, floppy drives and CD/DVD-ROM
  drives from disk image files, in virtual memory or by redirecting I/O
  requests somewhere else, possibly to another machine, through a
  co-operating user-mode service, ImDskSvc.

  To install this driver, service and command line tool, right-click on the
  imdisk.inf file and select 'Install'. To uninstall, use the Add/Remove
  Programs applet in the Control Panel.

  You can get syntax help to the command line tool by typing just imdisk
  without parameters.

  I have tested this product under 32-bit versions of Windows NT 3.51, NT 4.0,
  2000, XP, Server 2003, Vista, 7, 8, 8.1 and 10 and x86-64 versions of XP,
  Server 2003, Vista, 7, 8, 8.1 and 10. Primary target are older versions and
  there are several known compatibility issues on modern version of Windows.
  Please see website for more details: https://ltr-data.se/opencode.html#ImDisk

  The install/uninstall routines do not work under NT 3.51. If you want to use
  this product under NT 3.51 you have to manually add registry entries needed
  by driver and service or use resource kit tools to add necessary settings.

    Copyright (c) 2005-2021 Olof Lagerkvist
    https://www.ltr-data.se      olof@ltr-data.se

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
      https://web.archive.org/web/20100902032534/http://chitchat.at.infoseek.co.jp:80/vmware/vfd.html
    Copyright (C) Free Software Foundation, Inc.
    Read gpl.txt for the full GNU GPL license.

    This software may contain BSD licensed code:
    - Some code ported to NT from the FreeBSD md driver by Olof Lagerkvist.
      https://www.ltr-data.se
    Copyright (c) The FreeBSD Project.
    Copyright (c) The Regents of the University of California.

