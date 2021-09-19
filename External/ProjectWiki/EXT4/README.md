
***

# EXT4 file system

![EXT4.png](https://github.com/seanpm2001/WacOS/blob/master/System/FileSystem/EXT4/EXT4_FakingTransparency.jpeg)

_Note: I am aware that the image is not transparent. See more below._

Development Source: [[Unknown]](https://en.wikipedia.org/wiki/Ext4)

ext4 (fourth extended filesystem) is one of the 5 default file systems used in WacOS. This file system is good for various Linux installations.

## Installation notes

This is a standard Linux file system, and you should use it if you don't know what else to choose, and if you are familar with the format.

Some bonuses of EXT4 over NTFS (NTFS is not an option for WacOS) is the support for standard Windows-illegal characters in file names, such as `$` `:` `;` `'` and more.

## Limitations

[[From Wikipedia]](https://en.wikipedia.org/wiki/Ext4#Limitations)

In 2008, the principal developer of the ext3 and ext4 file systems, Theodore Ts'o, stated that although ext4 has improved features, it is not a major advance, it uses old technology, and is a stop-gap. Ts'o believes that Btrfs is the better direction because "it offers improvements in scalability, reliability, and ease of management". Btrfs also has "a number of the same design ideas that reiser3/4 had". However, ext4 has continued to gain new features such as file encryption and metadata checksums.

The ext4 file system does not honor the "secure deletion" file attribute, which is supposed to cause overwriting of files upon deletion. A patch to implement secure deletion was proposed in 2011, but did not solve the problem of sensitive data ending up in the file-system journal.

## Other/Misc

The current image I have is a stock image with fake PNG transparent textures. It would be nice to have a version that is actually transparent in PNG/SVG formaat, instead of JPEG format.

I am now considering making BTRFS a 6th optional default file system, but I need to be sure it is a better fit first.

***

## Article info

**Written on:** `2021 Saturday September 18th at 9:56 pm`

**Last revised on:** `2021 Saturday September 18th at 9:56 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (Saturday September 18th at 9:56 pm)`

***
