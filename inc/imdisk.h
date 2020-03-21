/*
    ImDisk Virtual Disk Driver for Windows NT/2000/XP.

    Copyright (C) 2005-2006 Olof Lagerkvist.

    Some credits:
    - Parts related to floppy emulation based on VFD by Ken Kato.
      http://chitchat.at.infoseek.co.jp/vmware/vfd.html
    - Parts related to CD-ROM emulation and impersonation to support remote
      files based on FileDisk by Bo Brantén.
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
*/

#ifndef _IMDISK_H
#define _IMDISK_H

#ifndef __T
#if defined(_NTDDK_) || defined(UNICODE) || defined(_UNICODE)
#define __T(x)  L ## x
#else
#define __T(x)  x
#endif
#endif

#ifndef _T
#define _T(x)   __T(x)
#endif

#define IMDISK_VERSION                 0x0101

///
/// The base names for the device objects created in \Device
///
#define IMDISK_DEVICE_DIR_NAME         _T("\\Device")
#define IMDISK_DEVICE_BASE_NAME        IMDISK_DEVICE_DIR_NAME  _T("\\ImDisk")
#define IMDISK_CTL_DEVICE_NAME         IMDISK_DEVICE_BASE_NAME _T("Ctl")

///
/// The symlinks to the device objects created in \DosDevices
///
#define IMDISK_CTL_DOSDEV              _T("ImDiskCtl")
#define IMDISK_CTL_DOSDEV_NAME         _T("\\\\.\\")        IMDISK_CTL_DOSDEV
#define IMDISK_CTL_SYMLINK_NAME        _T("\\DosDevices\\") IMDISK_CTL_DOSDEV

///
/// The driver name and image path
///
#define IMDISK_DRIVER_NAME             _T("ImDisk")
#define IMDISK_DRIVER_PATH             _T("system32\\drivers\\imdisk.sys")

///
/// Registry settings. It is possible to specify devices to be mounted
/// automatically when the driver loads.
///
#define IMDISK_CFG_PARAMETER_KEY       _T("\\Parameters")
#define IMDISK_CFG_MAX_DEVICES_VALUE   _T("MaxDevices")
#define IMDISK_CFG_LOAD_DEVICES_VALUE  _T("LoadDevices")
#define IMDISK_CFG_IMAGE_FILE_PREFIX   _T("FileName")
#define IMDISK_CFG_SIZE_PREFIX         _T("Size")
#define IMDISK_CFG_FLAGS_PREFIX        _T("Flags")

///
/// Base value for the IOCTL's.
///
#define FILE_DEVICE_IMDISK             0x8372

#define IOCTL_IMDISK_QUERY_VERSION      ((ULONG) CTL_CODE(FILE_DEVICE_IMDISK, 0x800, METHOD_BUFFERED, 0))
#define IOCTL_IMDISK_CREATE_DEVICE      ((ULONG) CTL_CODE(FILE_DEVICE_IMDISK, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS))
#define IOCTL_IMDISK_QUERY_DEVICE       ((ULONG) CTL_CODE(FILE_DEVICE_IMDISK, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS))
#define IOCTL_IMDISK_QUERY_DRIVER       ((ULONG) CTL_CODE(FILE_DEVICE_IMDISK, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS))

///
/// Bit constants for the Flags field in IMDISK_CREATE_DATA
///
#define IMDISK_OPTION_RO                0x00000001

#define IMDISK_READONLY(x)              ((ULONG)(x) & 0x00000001)

#define IMDISK_DEVICE_TYPE_HD           0x00000010
#define IMDISK_DEVICE_TYPE_FD           0x00000020
#define IMDISK_DEVICE_TYPE_CD           0x00000030

#define IMDISK_DEVICE_TYPE(x)           ((ULONG)(x) & 0x000000F0)

#define IMDISK_TYPE_FILE                0x00000100
#define IMDISK_TYPE_VM                  0x00000200
#define IMDISK_TYPE_PROXY               0x00000300

#define IMDISK_TYPE(x)                  ((ULONG)(x) & 0x00000F00)

#define IMDISK_PROXY_TYPE_DIRECT        0x00000000
#define IMDISK_PROXY_TYPE_COMM          0x00001000
#define IMDISK_PROXY_TYPE_TCP           0x00002000

#define IMDISK_PROXY_TYPE(x)            ((ULONG)(x) & 0x0000F000)

#define IMDISK_AUTO_DEVICE_NUMBER       ((ULONG)-1)

typedef struct _IMDISK_CREATE_DATA
{
  ULONG           DeviceNumber;
  DISK_GEOMETRY   DiskGeometry;
  ULONG           Flags;
  WCHAR           DriveLetter;
  USHORT          FileNameLength;
  WCHAR           FileName[1];
} IMDISK_CREATE_DATA, *PIMDISK_CREATE_DATA;

#endif
