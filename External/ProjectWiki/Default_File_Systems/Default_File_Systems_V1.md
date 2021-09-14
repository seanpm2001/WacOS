
***

# WacOS Default File Systems

## Overview

WacOS by default has 5 options for a file system upon install. The current 5 options are:

1. [HFS](/https://github.com/seanpm2001/WacOS/System/FileSystem/HFS/)

![HFS.png](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/HFS/HFS.png)

# HFS file system

Development Source: [[Unknown]](https://en.wikipedia.org/wiki/Hierarchical_File_System)

HFS (Hierarchical File System) is one of the 5 default file systems used in WacOS. If your system is very low end/a virtual machine, this file system will work well on most oldschool virtual machines. It was originally designed for Apple System 2.1 (1985) and isn't fully supported in Mac OS X 10.6 and up, and Windows Vista and up. It is kept for legacy purposes.

## Installation notes

It is recommended to only use this file system if you plan to install WacOS inside a virtual machine, or a computer before 2010.

## Limitations

The current limitations of this file system are the 65,536 file limit (max amount of files on the drive) and the 2 terabyte hard drive capacity. It was originally designed to run on floppy diskettes, and small hard drives from 1985 to ~2005.

Be careful with year bugs, as this file system only supports dates up to February 6th 2040.

This file system is incompatible with MacOS 10.15 and up, and Windows 7 and up.
***

2. [HFS+](/https://github.com/seanpm2001/WacOS/System/FileSystem/HFS+/)

![HFSPLUS.png](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/HFS%2B/HFSPLUS.png)

# HFS+ file system

Development Source: [[Unknown]](https://en.wikipedia.org/wiki/HFS_Plus)

HFS+ (Hierarchical File System Plus) is one of the 5 default file systems used in WacOS. If your system is capable, this file system will work well on low to mid-range to low-high range computers and supercomputers. The maximum disk size is 8 exabytes, and you have up to 8 exabytes per file. This is plenty for the majority of computer users.

## Installation notes

Make sure to use a newer version of HFS+ if you want support for drives larger than 2 terabytes. There is a bug with old versions of HFS+ on Linux (unknown when it got resolved since 2011) where the 8 exabyte limit is dropped to 2 terabytes.

Also, you should be 100% sure you aren't switching back to Windows or MacOS, as Windows 10 (as of build 20H9) does not support the HFS+ file system, and will not let you access its contents. MacOS 10.15 and above is the same way.

Although this file system works fine on Hard Disk Drives, if you can afford a Solid State Drive, you should really consider using one. They are much less prone to errors, as they don't have any moving parts.

## Limitations

The current limitations of this file system are the 256 character file path, the 8 exabyte storage size (which should be fine for 99.9% of current computers) and the Linux only compatibility.

It is not compatible with running WacOS on low-end virtual machines or computers with less than 2 gigabytes of memory or more than 8 exabytes of memory.

***

3. [EXT4](/https://github.com/seanpm2001/WacOS/System/FileSystem/EXT4/)

![EXT4.png](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/EXT4/EXT4_FakingTransparency.jpeg)

# EXT4 file system

Development Source: [[Unknown]](https://en.wikipedia.org/wiki/Ext4)

ext4 (fourth extended filesystem) is one of the 5 default file systems used in WacOS. This file system is good for various Linux installations.

## Installation notes

This is a standard Linux file system, and you should use it if you don't know what else to choose, and if you are familar with the format.

Some bonuses of EXT4 over NTFS (NTFS is not an option for WacOS) is the support for standard Windows-illegal characters in file names, such as `$, : ; '` and more.

## Limitations

[[From Wikipedia]](https://en.wikipedia.org/wiki/Ext4#Limitations)

In 2008, the principal developer of the ext3 and ext4 file systems, Theodore Ts'o, stated that although ext4 has improved features, it is not a major advance, it uses old technology, and is a stop-gap. Ts'o believes that Btrfs is the better direction because "it offers improvements in scalability, reliability, and ease of management". Btrfs also has "a number of the same design ideas that reiser3/4 had". However, ext4 has continued to gain new features such as file encryption and metadata checksums.

The ext4 file system does not honor the "secure deletion" file attribute, which is supposed to cause overwriting of files upon deletion. A patch to implement secure deletion was proposed in 2011, but did not solve the problem of sensitive data ending up in the file-system journal.

## Other/Misc

The current image I have is a stock image with fake PNG transparent textures. It would be nice to have a version that is actually transparent in PNG/SVG formaat, instead of JPEG format.

I am now considering making BTRFS a 6th optional default file system, but I need to be sure it is a better fit first.

***

4. [APFS](/https://github.com/seanpm2001/WacOS/System/FileSystem/APFS/)

![APFS.png](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/APFS/APFS.png)

# APFS file system

Development Source: [[Unknown]](https://en.wikipedia.org/wiki/Apple_File_System)

APFS (Apple File System) is one of the 5 default file systems used in WacOS. If you intend to switch to MacOS 11.0 or higher, this is a recommended file system.

## Installation notes

It is recommended to only use this file system if you intend a possibility to switch to MacOS 11.0 or higher and still want to remain Linux compatible.

## Limitations

The current limitations of this file system are the 8 Exbibytes, which is fine for most systems, but it also isn't compatible with MacOS 10.11 and below, and Windows 10.

The file system is currently proprietary, and will need to be reverse-engineered to be able to work with Linux.

***

5. [OpenZFS](/https://github.com/seanpm2001/WacOS/System/FileSystem/OpenZFS/)

![OpenZFS_logo.svg](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/OpenZFS/OpenZFS_logo.svg)

# OpenZFS file system

Development Source: [[GitHub]](https://github.com/openzfs/zfs) [[Other/Unknown]](https://github.com/openzfs/zfs)

OpenZFS is one of the 5 default file systems used in WacOS. If your system is capable, this file system will have more memory capacity than you will ever need, with support for 256 trillion yobibytes (1^128 bytes, or over 340 undecillion bytes, or to be exact, 340,282,366,920,938,463,463,374,607,431,768,211,455 bytes) 

The current limitations of this file system are supporting the file system, and the 256 ASCII file path (less when using multibyte characters such as Unicode characters)

It is not compatible with running WacOS on low-end virtual machines or computers with less than 512 gigabytes of memory.

***

## Experimental

Support for HFS, APFS, and OpenZFS is currently experimental. APFS might be the hardest, due to it being so new, and having to be reverse engineered first. HFS might be difficult as well due to how old the format is.

BTRFS is currently under consideration for being a 6th option for a default file system.

## Recommendations

For Apple fans: HFS+, APFS

For less-technical Apple fans: EXT4

For Apple fans with old Apple virtual machines: HFS

For everyone: EXT4

For massive data: OpenZFS

***

## Article info

**Written on:** `2021 Tuesday September 14th at 3:04 pm`

**Last revised on:** `2021 Tuesday September 14th at 3:04 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 2021 Tuesday September 14th at 3:04 pm`

***
