
***

<details open><summary><b lang="en">Click/tap here to expand/collapse the logo for this subproject</b></summary>

![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png)

</details>

| ![SadMac_Tiny64px_HighCompression.png](SadMac_Tiny64px_HighCompression.png) Note: 32 bit programs/applications are no longer supported in WacOS 10.15 and later |
|-----------------------------------------------------------------------------------------------|

| [Previous (10.14)](https://github.com/seanpm2001/WacOS_10.14/) | [Current (10.15)](https://github.com/seanpm2001/WacOS_10.15/) | [Next (11.x)](https://github.com/seanpm2001/WacOS_11/) |
|---|---|---|
| ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) |
| First boot | First boot | First boot |
| ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) |
| Default boot | Default boot | Default boot |
| WacOS 10.14 (Mojave) [Local](/WacOS_X/10.14/) | WacOS 10.15 (Mojave) [Local](/WacOS_X/10.15/) | WacOS 11.x (Catalina) [Local](/WacOS_X/XI/11/) |

# WacOS 10.15

WacOS 10.15 is an open source recreation of MacOS 10.15 (Catalina) It is part of the WacOS operating system project. 

## Language

The system is currently written in C, but will also support several other languages, including x86 Assembly, Objective-C, Objective-C++, Swift, Metal, and AppleScript

### Programming language support

Objective-C is bumped up to version 2.0 (As of Wac OS X 10.6)

Python version 2.7 is included (As of Wac OS X 10.15)

Perl version ? is included (As of WacOS 10.15)

Ruby 1.8.6 is included (As of Wac OS X 10.5)

Swift is now supported (Starting with WOAHS X 10.9)

Metal is now supported (Starting with WacOS 10.12)

Objective-C++ is now supported (as of WacOS 10.12)

## Features

Features to replicate

Source: [MacOS 10.15 - Wikipedia (en)](https://en.wikipedia.org/w/index.php?title=MacOS_Catalina&oldid=1089360673)

### System requirements

macOS Catalina officially runs on all standard configuration Macs that supported Mojave. 2010–2012 Mac Pros, which could run Mojave only with a GPU upgrade, are no longer supported. Catalina requires 4 GB of memory, an increase over the 2 GB required by Lion through Mojave.

- [x] iMac: Late 2012 or newer
- [x] iMac Pro: Late 2017
- [x] Mac Pro: Late 2013 or newer
- [x] Mac Mini: Late 2012 or newer
- [x] MacBook: Early 2015 or newer
- [x] MacBook Air: Mid 2012 or newer
- [x] MacBook Pro: Mid 2012 or newer, Retina display not needed

It is possible to install Catalina on many older Macintosh computers that are not officially supported by Apple. This requires using a patch to modify the install image.

### Applications

- [x] AirPort Utility
- [x] App Store
- [x] Archive Utility
- [x] Audio MIDI Setup
- [x] Automator
- [x] Bluetooth File Exchange
- [x] Books
- [x] Boot Camp Assistant
- [x] Calculator
- [x] Calendar
- [x] Chess
- [x] ColorSync Utility
- [x] Console
- [x] Contacts
- [x] Dictionary
- [x] Digital Color Meter
- [x] Disk Utility
- [x] DVD Player
- [x] FaceTime
- [x] Find My
- [x] Font Book
- [x] GarageBand (may not be pre-installed)
- [x] Grapher
- [x] Home
- [x] iMovie (may not be pre-installed)
- [x] Image Capture
- [x] Keychain Access
- [x] Keynote (may not be pre-installed)
- [x] Mail
- [x] Migration Assistant
- [x] Music
- [x] News (only available for Australia, Canada, United Kingdom, and United States)
- [x] Notes
- [x] Numbers (may not be pre-installed)
- [x] Pages (may not be pre-installed)
- [x] Photo Booth
- [x] Podcasts
- [x] Preview
- [x] QuickTime Player
- [x] Reminders
- [x] Screenshot (succeeded Grab since Mojave)
- [x] Script Editor
- [x] Stickies
- [x] Stocks
- [x] System Information
- [x] Terminal
- [x] TextEdit
- [x] Time Machine
- [x] TV
- [x] Voice Memos
- [x] VoiceOver Utility
- [x] X11/XQuartz (may not be pre-installed)

### Changes

#### System

##### Catalyst

Catalyst is a new software-development tool that allows developers to write apps that can run on macOS, iOS and iPadOS. Apple demonstrated several ported apps, including Jira and Twitter (after the latter discontinued its macOS app in February 2018).
System extensions

An upgrade from Kexts. System extensions avoid the problems of Kexts. There are 3 kinds of System extensions: Network Extensions, Endpoint Security Extensions, and Driver Extensions. System extensions run in userspace, outside of the kernel. Catalina will be the last version of macOS to support legacy system extensions.

##### DriverKit

A replacement for IOKit device drivers, driver extensions are built using DriverKit. DriverKit is a new SDK with all-new frameworks based on IOKit, but updated and modernized. It is designed for building device drivers in userspace, outside of the kernel.

##### Gatekeeper

Mac apps, installer packages, and kernel extensions that are signed with a Developer ID must be notarized by Apple to run on macOS Catalina.

##### Activation Lock

Activation Lock helps prevent the unauthorized use and drive erasure of devices with an Apple T2 security chip (2018, 2019, and 2020 MacBook Pro; 2020 5K iMac; 2018 MacBook Air, iMac Pro; 2018 Mac Mini; 2019 Mac Pro).

##### Dedicated system volume

The system runs on its own read-only volume, separate from all other data on the Mac.

##### Voice control

Users can give detailed voice commands to applications. On-device machine processing is used to offer better navigation.

##### Sidecar

Sidecar allows a Mac to use an iPad (running iPadOS) as a wireless external display. With Apple Pencil, the device can also be used as a graphics tablet for software running on the computer. Sidecar requires a Mac with Intel Skylake CPUs and newer (such as the fourth-generation MacBook Pro), and an iPad that supports Apple Pencil.

##### Support for wireless game controllers

The Game Controller framework adds support for two major console game controllers: the PlayStation 4's DualShock 4 and the Xbox One controller.

##### Time Machine

A number of under-the-hood changes were made to Time Machine, the backup software. For example, the manner in which backup data is stored on network-attached devices was changed, and this change is not backwards-compatible with earlier versions of macOS. Apple declined to document these changes, but some of them have been noted.

#### Applications

##### iTunes

iTunes is replaced by separate Music, Podcasts, TV and Books apps, in line with iOS. iOS device management is now conducted via Finder. The TV app on Mac supports Dolby Atmos, Dolby Vision, and HDR10 on MacBooks released in 2018 or later, while 4K HDR playback is supported on Macs released in 2018 or later when connected to a compatible display.

iTunes can still be installed and will work separately from the new apps (according to support information on Apple’s website).

##### Find My

Find My Mac and Find My Friends are merged into an application called Find My.

##### Notes

The Notes application was enhanced to allow better management of checklists and the ability to share folders with other users. The application version was incremented from 4.6 (in macOS 10.14 Mojave) to 4.7.

##### Reminders

Among other visual and functional overhauls, attachments can be added to reminders and Siri can intelligently estimate when to remind the user about an event.

##### Voice Memos

The Voice Memos application, first ported from iOS to the Mac in macOS 10.14 Mojave as version 2.0, was incremented to version 2.1.

#### Removed or changed components

macOS Catalina exclusively supports 64-bit applications. 32-bit applications no longer run (including all software that utilizes the Carbon API as well as QuickTime 7 applications, image, audio and video codecs). Apple has also removed all 32-bit-only apps from the Mac App Store.

Zsh is the default login shell and interactive shell in macOS Catalina, replacing Bash, the default shell since Mac OS X Panther in 2003. Bash continues to be available in macOS Catalina, along with other shells such as csh/tcsh and ksh.

Dashboard has been removed in macOS Catalina.

The ability to add Backgrounds in Photo Booth was removed in macOS Catalina.

The command-line interface GNU Emacs application was removed in macOS Catalina.

Built-in support for Perl, Python 2.7 and Ruby are included in macOS for compatibility with legacy software. Future versions of macOS will not include scripting language runtimes by default, possibly requiring users to install additional packages.

Legacy AirDrop for connecting with Macs running Mac OS X Lion, Mountain Lion and Mavericks, or 2011 and older Macs has been removed.

### Boot screen

The `Happy Wac` is disabled by default on boot to match the release (starting with Wac OS X 10.2) and on boot, the letter `W` will show. By default, on the first install, it will go with the brand logo (The W logo with no detail) then it will be the `Modern1` logo (The W logo with a metal texture) This can be changed in [`WACOS_10-15_BOOTCONFIG.cfg`](/10.15/WACOS_10-15_BOOTCONFIG.cfg)

### File system

OpenAPFS is now the default file system (starting with WacOS 10.12)

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

[Guesthouse repository](https://github.com/seanpm2001/WacOS_10.15/)

This is a guesthouse repository, and not a home repository, as development mainly stays on the main WacOS side. This is just the guesthouse that the project retreats to at times. If you are already in this repository, the link is likely recursive, and will reload the page.

[Home repository](https://github.com/seanpm2001/WacOS/tree/WacOS-dev/WacOS_X/10.15/)

This is the home repository. If you are already in this repository, the link is likely recursive, and will reload the page.

***

## File info

**File type:** `Markdown document (*.md *.mkd *.mdown *.markdown)`

**File version:** `1 (2022, Wednesday, June 8th at 2:02 pm PST)`

**Line count (including blank lines and compiler line):** `332`

**Current article language:** `English (USA)`

***
