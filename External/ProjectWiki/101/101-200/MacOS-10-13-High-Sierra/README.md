  
***

# MacOS 10.13 (High Sierra)

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.13_High_Sierra/MacOS_High_Sierra_wordmark.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.13_High_Sierra/MacOS_High_Sierra_wordmark.svg)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.13_High_Sierra/MacOS_High_Sierra_Desktop.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.13_High_Sierra/MacOS_High_Sierra_Desktop.png)

( **Predecessor:** [MacOS 10.12 (Sierra))](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-12-Sierra/) | **Successor:** [MacOS 10.14 (Mojave)](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/) )

macOS High Sierra (version 10.13) is the fourteenth major release of macOS, Apple Inc.'s desktop operating system for Macintosh computers. macOS High Sierra was announced at the WWDC 2017 on June 5, [2017](https://github.com/seanpm2001/WacOS/wiki/2017/) and was released on September 25, [2017](https://github.com/seanpm2001/WacOS/wiki/2017/). The name "High Sierra" refers to the High Sierra region in California. Following on from [macOS Sierra](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-12-Sierra/), its iterative name also alludes to its status as a refinement of its predecessor, focused on performance improvements and technical updates rather than user features. This makes it similar to previous macOS releases [Snow Leopard](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-6-Snow-Leopard/), [Mountain Lion](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-8-Mountain-Lion/) and [El Capitan](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-11-El-Capitan/). Among the apps with notable changes are Photos and Safari.

## System requirements

macOS High Sierra is supported on the following Macintosh computers:

iMac: Late 2009 or later

MacBook: Late 2009 or later

MacBook Pro: Mid 2010 or later

MacBook Air: Late 2010 or later

Mac Mini: Mid 2010 or later

Mac Pro: Mid 2010 or later

macOS High Sierra requires at least 2 GB of RAM and 14.3 GB of available disk space.

It is possible to install High Sierra on many older Macintosh computers that are not officially supported by Apple. This requires using a patch to modify the install image.

## Changes

### System

#### Apple File System

[Apple File System (APFS)](https://github.com/seanpm2001/WacOS/wiki/APFS/) replaces [HFS Plus](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/) as the default file system in macOS for the first time with High Sierra. It supports 64‑bit inode numbers, is designed for flash memory, and is designed to speed up common tasks like duplicating a file and finding the size of a folder's contents. It also has built‑in encryption, crash‑safe protections, and simplified data backup on the go.

#### Metal 2

Metal, Apple's low-level graphics API, has been updated to Metal 2. It includes virtual-reality and machine-learning features, as well as support for external GPUs. The system's windowing system, Quartz Compositor, supports Metal 2.

#### Media

macOS High Sierra adds support for High Efficiency Video Coding (HEVC), with hardware acceleration where available, as well as support for High Efficiency Image File Format (HEIF). Macs with the Intel Kaby Lake processor offer hardware support for Main 10 profile 10-bit hardware decoding, those with the Intel Skylake processor support Main profile 8-bit hardware decoding, and those with AMD Radeon 400 series graphics also support full HEVC decoding. However, whenever an Intel IGP is present, the frameworks will only direct requests to Intel IGP. In addition, audio codecs FLAC and Opus are also supported, but not in iTunes.

HEVC hardware acceleration requires a Mac with a sixth-generation Intel processor or newer (late 2015 27-inch iMac, mid 2017 21.5-inch iMac, early 2016 MacBook, late 2016 MacBook Pro or iMac Pro).

#### Other

Kernel extensions ("kexts") will require explicit approval by the user before being able to run.

The Low Battery notification and its icon were replaced by a flatter modern look.

The time service ntpd was replaced with timed for the time synchronization.

The FTP and telnet command line programs were removed.

Caching Server, File Sharing Server, and Time Machine Server, features that were previously part of macOS Server, are now provided as part of the OS.

The screen can now be locked using the shortcut Cmd+Ctrl+Q. The ability to lock screen using a menu bar shortcut activated in Keychain Access preferences has now been removed.

The 10.13.4 update added support for external graphics processors for Macs equipped with Thunderbolt 3 ports. The update discontinued support for external graphics processors in 2015 or older Macs, equipped with Thunderbolt 1 and 2 ports.

Starting with 10.13.4, when a 32-bit app is opened, users get a one-time warning about its future incompatibility with the macOS operating system.

## Applications

### Final Cut Pro 7

Apple announced the original Final Cut Studio suite of programs will not work on High Sierra. Media professionals that depend on any of those programs were advised to create a double boot drive to their computer.

### Photos

macOS High Sierra gives Photos an updated sidebar and new editing tools. Photos synchronizes tagged People with iOS 11.

### Mail

Mail has improved Spotlight search with Top Hits. Mail also uses 35% less storage space due to optimizations, and Mail's compose window can now be used in split-screen mode.

### Safari

macOS High Sierra includes Safari 11. Safari 11 has a new "Intelligent Tracking Prevention" feature that uses machine learning to block third parties from tracking the user's actions. Safari can also block auto playing videos from playing. The "Reader Mode" can be set to always-on. Safari 11 also supports WebAssembly. The last version of Safari that High Sierra supports is 13.1.2. This version has known security issues.

### Notes

The Notes app includes the ability to add tables to notes, and notes can be pinned to the top of the list. The version number was incremented to 4.5.

### Siri

Siri now uses a more natural and expressive voice. It also uses machine learning to understand the user better. Siri synchronizes information across iOS and Mac devices so the Siri experience is the same regardless of the product being used

### Messages

The release of macOS High Sierra 10.13.5 (and [iOS 11.4](https://github.com/seanpm2001/WacOS/wiki/iOS-11/)) introduced support for Messages in iCloud. This feature allows messages to sync across all devices using the same iCloud account. When messages are deleted they are deleted on each device as well, and messages stored in the cloud do not take up local storage on the device anymore. In order to use the feature, the user has to enable two-factor authentication for their Apple ID.

### Other applications found on macOS 10.13 High Sierra

AirPort Utility

App Store

Archive Utility

Audio MIDI Setup

Automator

Bluetooth File Exchange

Boot Camp Assistant

Calculator

Calendar

Chess

ColorSync Utility)

Console

Contacts

Dictionary

Digital Color Meter

Disk Utility

DVD Player

FaceTime

Font Book

Game Center

GarageBand (may not be pre-installed)

Grab

Grapher

iBooks (now Apple Books)

iMovie (may not be pre-installed)

iTunes

Image Capture

Ink (can only be accessed by connecting a graphics tablet to your Mac)

Keychain Access

Keynote (may not be pre-installed)

Migration Assistant

Numbers (may not be pre-installed)

Pages (may not be pre-installed)

Photo Booth

Preview

[QuickTime](https://github.com/seanpm2001/WacOS/wiki/QuickTime/) Player

Reminders

Script Editor

Stickies

System Information

Terminal

[TextEdit](https://github.com/seanpm2001/WacOS/wiki/TextEdit/)

Time Machine

VoiceOver Utility

X11/XQuartz (may not be pre-installed)

## Reception

In his September [2017](https://github.com/seanpm2001/WacOS/wiki/2017/) review of High Sierra, Roman Loyola, the senior editor of Macworld, gave it a provisionally positive review, calling it an "incremental update worthy of your time, eventually." Loyola expressed that the product's most significant draw was its security features, and that beyond this, the most beneficial changes lay in its future potential, saying it "doesn't have a lot of new features that will widen your eyes in excitement. But a lot of the changes are in the background and under the hood, and they lay a foundation for better things to come."

## Problems

macOS High Sierra 10.13.0 and 10.13.1 have a critical vulnerability that allowed an attacker to become a root user by entering "root" as a username, and not entering a password, when logging in. This was fixed in the Security Update 2017-001 for macOS High Sierra v10.13.1.

When it was first launched, it was discovered that the WindowServer process had a memory leak, leading to much slower graphics performance and lagging animations, probably due to some last-minute changes in Metal 2. This was fixed in macOS 10.13.1.

macOS High Sierra 10.13.4 had an error that caused DisplayLink to stop working for external monitors, allowing only one monitor to be extended. When using two external monitors, they could only be mirrored. Alban Rampon, the Senior Product Manager for DisplayLink, stated on December 24, 2018 that the company was working with Apple to resolve the issue.

## Release history

**Unsupported**

Version 	Build 	Date 	Darwin 	Notes 	Standalone download

10.13 	17A365 	September 25, 2017 	17.0.0 	Original Mac App Store release

About the security content of macOS High Sierra 10.13 	N/A

17A405 	October 5, 2017 	About the security content of macOS High Sierra 10.13 Supplemental Update 	macOS 10.13 Supplemental

10.13.1 	17B48 	October 31, 2017 	17.2.0 	About the macOS High Sierra 10.13.1 Update

About the security content of macOS High Sierra 10.13.1 	macOS High Sierra 10.13.1 update

17B1002 	November 29, 2017 	About the security content of Security Update 2017-001 	Security Update 2017-001 macOS High Sierra v10.13.1

17B1003

10.13.2 	17C88 	December 6, 2017 	17.3.0 	About the macOS High Sierra 10.13.2 Update

About the security content of macOS High Sierra 10.13.2 	macOS High Sierra 10.13.2 Update

macOS High Sierra 10.13.2 Combo Update

17C89

17C205 	January 8, 2018 	About the security content of macOS High Sierra 10.13.2 Supplemental Update 	macOS High Sierra 10.13.2 Supplemental

17C2205

10.13.3 	17D47 	January 23, 2018 	17.4.0 	About the macOS High Sierra 10.13.3 Update

About the security content of macOS High Sierra 10.13.3 	macOS High Sierra 10.13.3 Update

macOS High Sierra 10.13.3 Combo Update

17D2047

17D102 	February 19, 2018 	About the security content of macOS High Sierra 10.13.3 Supplemental Update 	macOS High Sierra 10.13.3 Supplemental

17D2102

10.13.4 	17E199 	March 29, 2018 	17.5.0 	About the macOS High Sierra 10.13.4 Update

About the security content of macOS High Sierra 10.13.4 	macOS High Sierra 10.13.4 Update

macOS High Sierra 10.13.4 Combo Update

17E202 	April 24, 2018 	About the security content of Security Update 2018-001 	Security Update 2018-001 macOS High Sierra v10.13.4

10.13.5 	17F77 	June 1, 2018 	17.6.0 	About the macOS High Sierra 10.13.5 Update

About the security content of macOS High Sierra 10.13.5 	macOS High Sierra 10.13.5 Update

macOS High Sierra 10.13.5 Combo Update

10.13.6 	17G66 	July 9, 2018 	17.7.0 	About the macOS High Sierra 10.13.6 Update

About the security content of macOS High Sierra 10.13.6 	macOS High Sierra 10.13.6 Update

macOS High Sierra 10.13.6 Combo Update

17G2208

17G3025 	October 30, 2018 	17.7.0 	About the security content of Security Update 2018-002 High Sierra 	Security Update 2018-002 High Sierra

17G4015 	December 5, 2018 	17.7.0 	About the security content of Security Update 2018-003 High Sierra 	Security Update 2018-003 High Sierra

17G5019 	January 22, 2019 	17.7.0 	About the security content of Security Update 2019-001 High Sierra 	Security Update 2019-001 High Sierra

17G6029 	March 25, 2019 	17.7.0 	About the security content of Security Update 2019-002 High Sierra 	Security Update 2019-002 High Sierra

17G6030 	March 29, 2019 	17.7.0 	About the security content of Security Update 2019-002 High Sierra 	Security Update 2019-002 High Sierra

17G7024 	May 13, 2019 	17.7.0 	About the security content of Security Update 2019-003 High Sierra 	Security Update 2019-003 High Sierra

17G8029 	July 22, 2019 	17.7.0 	About the security content of Security Update 2019-004 High Sierra 	Security Update 2019-004 High Sierra

17G8030 	July 29, 2019 	17.7.0 	About the security content of Security Update 2019-004 High Sierra 	Security Update 2019-004 High Sierra

17G8037 	September 26, 2019 	17.7.0 	About the security content of Security Update 2019-005 High Sierra 	Security Update 2019-005 High Sierra

17G9016 	October 29, 2019 	17.7.0 	About the security content of Security Update 2019-006 High Sierra 	Security Update 2019-006 High Sierra

17G10021 	December 10, 2019 	17.7.0

xnu-4570.71.63~1 	About the security content of Security Update 2019-007 High Sierra 	Security Update 2019-007 High Sierra

17G11023 	January 28, 2020 	17.7.0

xnu-4570.71.69~1 	About the security content of Security Update 2020-001 High Sierra 	Security Update 2020-001 High Sierra

17G12034 	March 24, 2020 	17.7.0

xnu-4570.71.73~1 	About the security content of Security Update 2020-002 High Sierra 	Security Update 2020-002 High Sierra

17G13033 	May 26, 2020 	17.7.0

xnu-4570.71.80~1 	About the security content of Security Update 2020-003 High Sierra 	Security Update 2020-003 High Sierra

17G13035 	June 1, 2020 	17.7.0

xnu-4570.71.80.1~1 	About the security content of Security Update 2020-003 High Sierra 	Security Update 2020-003 High Sierra

17G14019 	July 15, 2020 	17.7.0

xnu-4570.71.82.5~1 	About the security content of Security Update 2020-004 High Sierra 	Security Update 2020-004 High Sierra

17G14033 	September 24, 2020 	17.7.0

xnu-4570.71.82.6~1 	About the security content of Security Update 2020-005 High Sierra 	Security Update 2020-005 High Sierra

17G14042 	November 12, 2020 	17.7.0

xnu-4570.71.82.8~1 	About the security content of Security Update 2020-006 High Sierra 	Security Update 2020-006 High Sierra

**This article is a modified copy of the Wikipedia article of the same subject. It needs to be rewritten to be more original.**

***

## Sources

[Wikipedia - MacOS 10.13 (High Sierra)](https://en.wikipedia.org/wiki/MacOS_High_Sierra)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Friday September 24th at 4:30 pm`

**Last revised on:** `2021 Friday September 24th at 4:30 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Friday September 24th at 4:30 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
