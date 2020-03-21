
  ImDisk Virtual Disk Driver for Windows NT/2000/XP.

  This driver emulates harddisk partitions, floppy drives and CD/DVD-ROM
  drives from disk image files, in virtual memory or by redirecting I/O
  requests somewhere else, possibly to another machine, through a
  co-operating user-mode service, ImDskSvc.

  To install this driver, service and command line tool, run the install.cmd
  file or right-click the imdisk.inf file and select install. To uninstall,
  run uninstall.cmd or use the Add/Remove Programs applet in the Control
  Panel.

  You can get syntax help to the command line tool by typing just imdisk
  without parameters.

  I have tested this product under Windows NT 3.51, NT 4.0, 2000, XP and
  Server 2003. The install/uninstall routines do not work under NT 3.51. If
  you want to use this product under NT 3.51 you have to manually add the
  registry entries for the driver and the service.


    Copyright (C) 2005-2006 Olof Lagerkvist.

    Some credits:
    - Parts related to floppy emulation based on VFD by Ken Kato.
      http://chitchat.at.infoseek.co.jp/vmware/vfd.html
    - Parts related to CD-ROM emulation and impersonation to support remote
      files based on FileDisk by Bo BrantÅÈn.
      http://www.acc.umu.se/~bosse/
    - Virtual memory image support, usermode storage backend support and some
      code ported to NT from the FreeBSD md driver by Olof Lagerkvist.
      http://www.ltr-data.se

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
