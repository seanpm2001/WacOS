
***

<details open><summary><b lang="en">Click/tap here to expand/collapse the logo for this subproject</b></summary>

![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png)

</details>

| ![SadMac_Tiny64px_HighCompression.png](SadMac_Tiny64px_HighCompression.png) Note: WacOS 10.13 is the last version of WacOS to natively support 32 bit software. Support for 32 bit software will be terminated with WacOS 10.14 and up|
|-----------------------------------------------------------------------------------------------|

| [Previous (10.12)](https://github.com/seanpm2001/WacOS_10.12/) | [Current (10.13)](https://github.com/seanpm2001/WacOS_10.13/) | [Next (10.14)](https://github.com/seanpm2001/WacOS_10.14/) |
|---|---|---|
| ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) |
| First boot | First boot | First boot |
| ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) |
| Default boot | Default boot | Default boot |
| WacOS 10.12 (Sierra) [Local](/WacOS_X/10.12/) | WacOS 10.13 (High Sierra) [Local](/WacOS_X/10.13/) | WacOS 10.14 (Mojave) [Local](/WacOS_X/10.14/) |

# WacOS 10.13

WacOS 10.13 is an open source recreation of MacOS 10.13 (High Sierra) It is part of the WacOS operating system project. 

## Language

The system is currently written in C, but will also support several other languages, including x86 Assembly, Objective-C, Objective-C++, Swift, Metal, and AppleScript

### Programming language support

Objective-C is bumped up to version 2.0 (As of Wac OS X 10.6)

Python version 2.5 is included (As of Wac OS X 10.5)

Ruby 1.8.6 is included (As of Wac OS X 10.5)

Swift is now supported (Starting with WOAHS X 10.9)

Metal is now supported (Starting with WacOS 10.12)

> Metal has been bumped up to version 2 (Starting with WacOS 10.13)

Objective-C++ is now supported (as of WacOS 10.12)

## Features

Features to replicate

Source: [MacOS 10.13 - Wikipedia (en)(https://en.wikipedia.org/w/index.php?title=MacOS_High_Sierra&oldid=1091862307)

### System

#### Apple File System

Apple File System (APFS) replaces HFS Plus as the default file system in macOS for the first time with High Sierra. It supports 64‑bit inode numbers, is designed for flash memory, and is designed to speed up common tasks like duplicating a file and finding the size of a folder's contents. It also has built‑in encryption, crash‑safe protections, and simplified data backup on the go.

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

### Applications

#### Final Cut Pro 7

Apple announced the original Final Cut Studio suite of programs will not work on High Sierra. Media professionals that depend on any of those programs were advised to create a double boot drive to their computer.

#### Photos

macOS High Sierra gives Photos an updated sidebar and new editing tools. Photos synchronizes tagged People with iOS 11.

#### Mail

Mail has improved Spotlight search with Top Hits. Mail also uses 35% less storage space due to optimizations, and Mail's compose window can now be used in split-screen mode.

#### Safari

macOS High Sierra includes Safari 11. Safari 11 has a new "Intelligent Tracking Prevention" feature that uses machine learning to block third parties from tracking the user's actions. Safari can also block auto playing videos from playing. The "Reader Mode" can be set to always-on. Safari 11 also supports WebAssembly. The last version of Safari that High Sierra supports is 13.1.2. This version has known security issues.

#### Notes

The Notes app includes the ability to add tables to notes, and notes can be pinned to the top of the list. The version number was incremented to 4.5.

#### Siri

Siri now uses a more natural and expressive voice. It also uses machine learning to understand the user better. Siri synchronizes information across iOS and Mac devices so the Siri experience is the same regardless of the product being used.

#### Messages

The release of macOS High Sierra 10.13.5 (and iOS 11.4) introduced support for Messages in iCloud. This feature allows messages to sync across all devices using the same iCloud account. When messages are deleted they are deleted on each device as well, and messages stored in the cloud do not take up local storage on the device anymore. In order to use the feature, the user has to enable two-factor authentication for their Apple ID.

#### Other applications found on macOS 10.13 High Sierra

- [x] AirPort Utility
- [x] App Store
- [x] Archive Utility
- [x] Audio MIDI Setup
- [x] Automator
- [x] Bluetooth File Exchange
- [x] Boot Camp Assistant
- [x] Calculator
- [x] Calendar
- [x] Chess
- [x] ColorSync Utility)
- [x] Console
- [x] Contacts
- [x] Dictionary
- [x] Digital Color Meter
- [x] Disk Utility
- [x] DVD Player
- [x] FaceTime
- [x] Font Book
- [x] Game Center
- [x] GarageBand (may not be pre-installed)
- [x] Grab
- [x] Grapher
- [x] iBooks (now Apple Books)
- [x] iMovie (may not be pre-installed)
- [x] iTunes
- [x] Image Capture
- [x] Ink (can only be accessed by connecting a graphics tablet to your Mac)
- [x] Keychain Access
- [x] Keynote (may not be pre-installed)
- [x] Migration Assistant
- [x] Numbers (may not be pre-installed)
- [x] Pages (may not be pre-installed)
- [x] Photo Booth
- [x] Preview
- [x] QuickTime Player
- [x] Reminders
- [x] Script Editor
- [x] Stickies
- [x] System Information
- [x] Terminal
- [x] TextEdit
- [x] Time Machine
- [x] VoiceOver Utility
- [x] X11/XQuartz (may not be pre-installed)

### Reception

In his September 2017 review of High Sierra, Roman Loyola, the senior editor of Macworld, gave it a provisionally positive review, calling it an "incremental update worthy of your time, eventually." Loyola expressed that the product's most significant draw was its security features, and that beyond this, the most beneficial changes lay in its future potential, saying it "doesn't have a lot of new features that will widen your eyes in excitement. But a lot of the changes are in the background and under the hood, and they lay a foundation for better things to come."

#### Problems

macOS High Sierra 10.13.0 and 10.13.1 have a critical vulnerability that allowed an attacker to become a root user by entering "root" as a username, and not entering a password, when logging in. This was fixed in the Security Update 2017-001 for macOS High Sierra v10.13.1.

When it was first launched, it was discovered that the WindowServer process had a memory leak, leading to much slower graphics performance and lagging animations, probably due to some last-minute changes in Metal 2. This was fixed in macOS 10.13.1.

macOS High Sierra 10.13.4 had an error that caused DisplayLink to stop working for external monitors, allowing only one monitor to be extended. When using two external monitors, they could only be mirrored. Alban Rampon, the Senior Product Manager for DisplayLink, stated on December 24, 2018 that the company was working with Apple to resolve the issue.

### Boot screen

The `Happy Wac` is disabled by default on boot to match the release (starting with Wac OS X 10.2) and on boot, the letter `W` will show. By default, on the first install, it will go with the brand logo (The W logo with no detail) then it will be the `Modern1` logo (The W logo with a metal texture) This can be changed in [`WACOS_10-13-BOOTCONFIG.cfg`](/10.13/WACOS_10-13_BOOTCONFIG.cfg)

### File system

OpenAPFS is now the default fils system (starting with WacOS 10.12)

OpenHFS+ 2.0 is still a file system option, along with OpenZFS, which is still in read-only mode.

### Codecs

No new codecs are supported in this release.

### Applications found on Mac OS X 10.4

- [ ] Address Book
- [ ] AppleScript
- [ ] Calculator
- [ ] Chess
- [ ] Clock
- [ ] CPU Monitor
- [ ] DVD Player
- [ ] Image Capture
- [ ] iMovie
- [ ] Internet Connect
- [ ] iTunes
- [ ] Mail
- [ ] Preview
- [ ] Activity Monitor
- [ ] QuickTime Player
- [ ] Sherlock
- [ ] Stickies
- [ ] System Preferences
- [ ] StuffIt Expander
- [ ] TextEdit
- [ ] Terminal
- [ ] Font Book
- [ ] FileVault
- [ ] iChat AV
- [ ] X11
- [ ] Safari
- [ ] Dashboard
- [ ] Automator
- [ ] Grapher
- [ ] Dictionary
- [ ] Quartz Composer
- [ ] AU Lab
- [ ] Bootcamp
- [ ] Back to my mac
- [ ] App Store

To add to this list:

```
AirPort Utility
App Store
Archive Utility
Audio MIDI Setup
Automator
Bluetooth File Exchange
Boot Camp Assistant
Calculator
ColorSync Utility)
Console
Contacts
Dictionary
Digital Color Meter
Disk Utility
DVD Player
FaceTime
Font Book
GarageBand (may not be pre-installed)
Grab
Grapher
iMovie (may not be pre-installed)
iTunes
Image Capture
Keychain Access
Keynote (may not be pre-installed)
Messages
Migration Assistant
Notes
Notification Center
Numbers (may not be pre-installed)
Pages (may not be pre-installed)
Photo Booth
QuickTime Player
Script Editor
Stickies
System Information
Terminal
TextEdit
VoiceOver Utility
```

## Feature translation notes

The exact system requirements are not a forced emulation option. The WacOS system is designed to be lighter, but you can adjust it to match MacOS.

WacOS equivalents of programs are included.

Malicious methods (such as DRM/TPM) are NEVER included with WacOS, not even as an open source recreation.

Please [raise an issue](https://github.com/seanpm2001/WacOS/issues/) if any other clarification is needed.

## Home repositories

[Guesthouse repository](https://github.com/seanpm2001/WacOS_10.13/)

This is a guesthouse repository, and not a home repository, as development mainly stays on the main WacOS side. This is just the guesthouse that the project retreats to at times. If you are already in this repository, the link is likely recursive, and will reload the page.

[Home repository](https://github.com/seanpm2001/WacOS/tree/WacOS-dev/WacOS_X/10.13/)

This is the home repository. If you are already in this repository, the link is likely recursive, and will reload the page.

***

## File info

**File type:** `Markdown document (*.md *.mkd *.mdown *.markdown)`

**File version:** `1 (2022, Wednesday, June 8th at 2:04 pm PST)`

**Line count (including blank lines and compiler line):** `304`

**Current article language:** `English (USA)`

***
