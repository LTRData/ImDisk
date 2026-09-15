# ImDisk Virtual Disk Driver

ImDisk is a virtual disk driver for Windows. It emulates hard disk partitions, floppy drives and CD/DVD-ROM drives backed by disk image files, virtual memory, or redirected I/O through a cooperating service or proxy server.

## Project status and compatibility

**ImDisk uses a legacy design and is not recommended for recent versions of Windows.** Its design preserves compatibility with systems as old as Windows NT 3.51, and many applications written for Windows Vista and later require features it does not provide.

No new features are planned. Maintenance has continued: the May 2026 changes include a driver bug fix, installer and signing updates, and an ImDiskNet update. This does not remove the design limitations on modern Windows, including Windows 10 and 11.

Further feature development is focused on [Arsenal Image Mounter](https://github.com/ArsenalRecon/Arsenal-Image-Mounter). It emulates complete disks and works in many scenarios where applications expect physical disks. ImDisk remains available for older systems and specific uses that fit its partition/volume-level design.

Historically tested systems include 32-bit Windows NT 3.51, NT 4.0, 2000, XP, Server 2003, Vista, 7, 8, 8.1 and 10, and x86-64 Windows XP, Server 2003, Vista, 7, 8, 8.1 and 10. This is a record of past testing, not a statement that every current build supports every listed system. See the [project website](https://ltr-data.se/opencode.html#ImDisk) for downloads and compatibility details.

## Repository contents

| Component | Purpose |
|---|---|
| [sys](https://github.com/LTRData/ImDisk/tree/master/sys) | Kernel-mode driver, `imdisk.sys`. |
| [cli](https://github.com/LTRData/ImDisk/tree/master/cli) | Command-line tool, `imdisk.exe`. |
| [cpl](https://github.com/LTRData/ImDisk/tree/master/cpl) / [cplcore](https://github.com/LTRData/ImDisk/tree/master/cplcore) | Control Panel applet and core native API library. |
| [svc](https://github.com/LTRData/ImDisk/tree/master/svc) | `ImDskSvc`, the helper service that forwards proxy I/O over TCP/IP or serial connections. |
| [devio](https://github.com/LTRData/ImDisk/tree/master/devio) | Native server for ImDisk proxy operation. |
| [ImDiskNet](https://github.com/LTRData/ImDisk/tree/master/ImDiskNet) | VB.NET projects: the ImDisk API wrapper, DevioNet client/server library, and DiscUtilsDevio image-format integration. |

### AWEAlloc and DevIoDrv have moved

AWEAlloc and DevIoDrv were removed from this repository in May 2026. Their source now lives in Arsenal Image Mounter: [AWEAlloc](https://github.com/ArsenalRecon/Arsenal-Image-Mounter/tree/master/Unmanaged%20Source/awealloc) and [DevIoDrv](https://github.com/ArsenalRecon/Arsenal-Image-Mounter/tree/master/Unmanaged%20Source/deviodrv).

The driver files can be installed with Arsenal Image Mounter, including its free versions. Those versions of `awealloc.sys` and `deviodrv.sys` can also be used with ImDisk. Older setup instructions may still refer to an AWEAlloc copy bundled with ImDisk; obtain that driver from Arsenal Image Mounter instead.

## Installation and usage

Use the distribution from the [project website](https://ltr-data.se/opencode.html#ImDisk). The installation instructions apply to an extracted distribution containing the built driver, service and tools, rather than just this source checkout.

To install those components, right-click `imdisk.inf` and select **Install**. To uninstall, use **Add/Remove Programs** in Control Panel.

Run `imdisk` without parameters for command-line syntax. The [FAQ](https://github.com/LTRData/ImDisk/wiki/FAQ) covers RAM disks, image files, proxy operation and mount points.

The historical NT 3.51 install/uninstall routines require manual registry setup or resource-kit tools. ARM and ARM64 installations also require manual setup; see the [ARM64 setup guide](https://github.com/LTRData/ImDisk/wiki/ARM64-setup), together with the AWEAlloc migration note above.

## Building from source

The native projects are collected in [ImDisk.sln](https://github.com/LTRData/ImDisk/blob/master/ImDisk.sln). Toolchain requirements vary by configuration: the project files retain older Visual C++ toolsets, WDK 8.1/10 driver configurations and WDK 7 property sheets. The root `Makefile` is for the older WDK build environment and distribution packaging. Check the selected project's configuration and property sheets when setting up a build.

The managed projects are collected in [ImDiskNet.slnx](https://github.com/LTRData/ImDisk/blob/master/ImDiskNet/ImDiskNet.slnx). Their targets include .NET 8, 9 and 10 alongside older .NET Framework targets; ImDiskNet and DevioNet also target .NET Standard. These managed targets do not change the Windows driver requirements for mounting an ImDisk device.

## Copyright and licensing

See [LICENSE.md](https://github.com/LTRData/ImDisk/blob/master/LICENSE.md) for copyright notices, license terms and information about alternative commercial licensing.
