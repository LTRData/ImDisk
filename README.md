# ImDisk Virtual Disk Driver

ImDisk is a virtual disk driver for Windows. It emulates hard disk partitions, floppy drives and CD/DVD-ROM drives backed by disk image files, virtual memory, or redirected I/O through a cooperating service or proxy server.

## Project status and compatibility

**ImDisk uses a legacy design and is not recommended for recent versions of Windows.** Its design preserves compatibility with systems as old as Windows NT 3.51, and many applications written for Windows Vista and later require features it does not provide.

No new features are planned for this repository. Maintenance has continued: the May 2026 changes include a driver bug fix, installer and signing updates, and an ImDiskNet update. This does not remove the design limitations on modern Windows, including Windows 10 and 11.

Further feature development by the original author is focused on [Arsenal Image Mounter](https://github.com/ArsenalRecon/Arsenal-Image-Mounter). It emulates complete disks and works in many scenarios where applications expect physical disks. ImDisk remains available for older systems and specific uses that fit its partition/volume-level design.

Historically tested systems include 32-bit Windows NT 3.51, NT 4.0, 2000, XP, Server 2003, Vista, 7, 8, 8.1 and 10, and x86-64 Windows XP, Server 2003, Vista, 7, 8, 8.1 and 10. This is a record of past testing, not a statement that every current build supports every listed system. See the [project website](https://ltr-data.se/opencode.html#ImDisk) for downloads and compatibility details.

### Community forks

Development also continues in independently maintained forks. The following selection highlights forks with substantive changes in 2026, checked in September 2026:

| Fork | Features and focus |
|---|---|
| [DavidXanatos/ImDisk](https://github.com/DavidXanatos/ImDisk) | Adds Windows Mount Manager integration to address Windows 11 24H2 compatibility, automatic device removal if a user-mode proxy crashes, and subsequent Windows 10 and app-package compatibility fixes. Targets x64 and ARM64. See its [changelog](https://github.com/DavidXanatos/ImDisk/blob/master/CHANGELOG.md). |
| [lxl66566/ImDisk](https://github.com/lxl66566/ImDisk) | Builds on DavidXanatos's fork. Adds a path that completes eligible RAM-disk reads and writes directly in the calling thread, plus fixes for byte-swapped writes, zero-buffer detection and I/O control buffer validation. See the [September 2026 changes](https://github.com/lxl66566/ImDisk/compare/af28678be99a5df5005d56096ebfcd4005df812d...355ef98632f2538eb275084fc3132d44cd74e3c5). |
| [shadowjohn/ImDisk](https://github.com/shadowjohn/ImDisk) | Adds a WPF management GUI with built-in disk benchmarks, configurable automatic saving to image files, and controls for expanding mounted RAM disks. The GUI uses the original ImDisk driver binaries. See its [release notes](https://github.com/shadowjohn/ImDisk/releases/tag/v1.01). |

See each fork's documentation and releases for its current maintenance status, supported Windows versions and installation instructions.

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
