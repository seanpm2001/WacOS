
***

<details open><summary><b lang="en">Click/tap here to expand/collapse the logo for this subproject</b></summary>

![/W_Cats.png](/W_Cats.png)

</details>

| ![SadMac_Tiny64px_HighCompression.png](/SadMac_Tiny64px_HighCompression.png) Note: Wac OS X 10.3 is the last version where an Internet Explorer equivalent for WacOS is installed by default (and, or supported) |
|-----------------------------------------------------------------------------------------------|

| [Previous (10.2)](https://github.com/seanpm2001/WacOS_X_10.2/) | [Current (10.3)](https://github.com/seanpm2001/WacOS_X_10.3) | [Next (10.4)](https://github.com/seanpm2001/WacOS_X_10.4) |
|---|---|---|
| ![/W_Cats_HighCompression.png](/W_Cats_HighCompression.png) | ![/W_Cats_HighCompression.png](/W_Cats_HighCompression.png) | ![/W_Cats_HighCompression.png](/W_Cats_HighCompression.png) |
| First boot | First boot | First boot |
| ![/W_Modern1_HighCompression.png](/W_Modern1_HighCompression.png) |  ![/W_Modern1_HighCompression.png](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression.png](/W_Modern1_HighCompression.png) |
| Default boot | Default boot | Default boot |
| Wac OS X 10.2 (Jaguar) [Local](/WacOS_X/10.2/) | Wac OS X 10.3 (Panther) [Local](/WacOS_X/10.3/) | Wac OS X 10.4 (Tiger) [Local](/WacOS_X/10.4/) |

# WacOS X 10.3

Wac OS X 10.3 is an open source recreation of Mac OS X 10.3. It is part of the WacOS operating system project. 

## Language

The system is currently written in C, but will also support several other languages, including x86 Assembly, Objective-C, and AppleScript

## Features

Features to replicate

Source: [Mac OS X 10.3 - Wikipedia (en)](https://en.wikipedia.org/w/index.php?title=Mac_OS_X_Panther&oldid=1086547932)

Panther's system requirements are:

- [ ] PowerPC G3, G4, or G5 processor (at least 233 MHz)
- [ ] Built-in USB
- [ ] At least 128 MB of RAM (256 MB recommended, minimum of 96 MB supported unofficially)
- [ ] At least 1.5 GB of available hard disk space
- [ ] CD drive
- [ ] Internet access requires a compatible service provider; iDisk requires a .Mac account

Video conferencing requires:

- [ ] 333 MHz or faster PowerPC G3, G4, or G5 processor
- [ ] Broadband internet access (100 kbit/s or faster)
- [ ] Compatible FireWire DV camera or web camera

Since a New World ROM was required for Mac OS X Panther, certain older computers (such as beige Power Mac G3s and 'Wall Street' PowerBook G3s) were unable to run Panther by default. Third-party software (such as XPostFacto) can, however, override checks made during the install process; otherwise, installation or upgrades from Jaguar fails on these older machines.

Panther still fully supported the Classic environment for running older Mac OS 9 applications, but made Classic application windows double-buffered, interfering with some applications written to draw directly to screen.

### New and changed features

#### End-user features

Apple advertised that Mac OS X Panther had over 150 new features, including:

- [ ] Finder: Updated with a brushed-metal interface, a new live search engine, customizable Sidebar, secure deletion, colored labels (resurrected from classic Mac OS) in the filesystem and Zip support built in. The Finder icon was also changed.
- [ ] Fast user switching: Allows a user to remain logged in while another user logs in, and quickly switch among several sessions.
- [ ] Exposé: Helps the user manage windows by showing them all as thumbnails.
- [ ] TextEdit: TextEdit now is also compatible with Microsoft Word (.doc) documents.
- [ ] Xcode developer tools: Faster compile times with gcc 3.3.
- [ ] Preview: Increased speed of PDF rendering.
- [ ] QuickTime: Now supports the Pixlet high-definition video codec.

#### New applications in Panther

- [ ] Font Book: A font manager which simplifies viewing character maps, and adding new fonts that can be used systemwide. The app also allows the user to organize fonts into collections.
- [ ] FileVault: On-the-fly encryption and decryption of a user's home folder.
- [ ] iChat AV: The new version of iChat. Now with built-in audio- and video conferencing.
- [ ] X11: X11 is built into Panther.
- [ ] Safari: A new web browser that was developed to replace Internet Explorer for Mac when the contract between Apple and Microsoft ended, although Internet Explorer for Mac was still available. Safari 1.0 was included in an update in Jaguar but was used as the default browser in Panther.

#### Other

- [ ] Microsoft Windows interoperability improvements, including out-of-the-box support for Active Directory and SecurID-based VPNs.
- [ ] Built-in fax support.

### Boot screen

The `Happy Wac` is disabled by default on boot to match the release, and on boot, the letter `W` will show. By default, on the first install, it will go with the brand logo (The W logo with a big cat texture) then it will be the `Modern1` logo (The W logo with a metal tecxturr) This can be changed in [`WACOS_10-2_BOOT.cfg`](/10.2/WACOS_10-2_BOOT.cfg)

### File system

OpenHFS+ 2.0 is still the default file system.

### Codecs

No new codecs are supported in this release.

### Applications found on Mac OS X 10.3

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
- [ ] Internet Explorer for Mac
- [ ] iTunes
- [ ] Mail
- [ ] Preview
- [ ] Process Viewer (now Activity Monitor)
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

## Home repositories

[Guesthouse repository](https://github.com/seanpm2001/WacOS_X_10.3/)

This is a guesthouse repository, and not a home repository, as development mainly stays on the main WacOS side. This is just the guesthouse that the project retreats to at times. If you are already in this repository, the link is likely recursive, and will reload the page.

[Home repository](https://github.com/seanpm2001/WacOS/tree/WacOS-dev/WacOS_X/10.3/)

This is the home repository. If you are already in this repository, the link is likely recursive, and will reload the page.

***

## File info

**File type:** `Markdown document (*.md *.mkd *.mdown *.markdown)`

**File version:** `2 (2022, Saturday, June 4th at 6:52 pm PST)`

> Version 2: There was originally only a small revision, so I did a little extra (I added a feature deprecation warning, and added documentation for this version) I didn't want this to be a 1 byte change release

**Line count (including blank lines and compiler line):** `148`

**Current article language:** `English (USA)`

***
