
***

# HFS+ file system

![HFSPLUS.png](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/HFS%2B/HFSPLUS.png)

Development Source: [[Unknown]](https://en.wikipedia.org/wiki/HFS_Plus)

HFS+ (Hierarchical File System Plus) is one of the 5 default file systems used in WacOS. If your system is capable, this file system will work well on low to mid-range to low-high range computers and supercomputers. The maximum disk size is 8 exabytes, and you can have up to 8 exabytes per file. This is plenty for the majority of computer users.

## Installation notes

Make sure to use a newer version of HFS+ if you want support for drives larger than 2 terabytes. There is a bug with old versions of HFS+ on Linux (unknown when it got resolved since 2011) where the 8 exabyte limit is dropped to 2 terabytes.

Also, you should be 100% sure you aren't switching back to Windows or MacOS, as Windows 10 (as of build 20H9) does not support the HFS+ file system, and will not let you access its contents. MacOS 10.15 and above is the same way.

Although this file system works fine on Hard Disk Drives, if you can afford a Solid State Drive, you should really consider using one. They are much less prone to errors, as they don't have any moving parts.

## Limitations

The current limitations of this file system are the 256 character file path, the 8 exabyte storage size (which should be fine for 99.9% of current computers) and the Linux only compatibility.

It is not compatible with running WacOS on low-end virtual machines or computers with less than 2 gigabytes of memory or more than 8 exabytes of memory.

***

## Article info

**Written on:** `2021 Saturday September 18th at 10:01 pm`

**Last revised on:** `2021 Saturday September 18th at 10:01 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (Saturday September 18th at 10:01 pm)`

***
