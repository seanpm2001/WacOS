
***

<details open><summary><b lang="en">Click/tap here to expand/collapse the logo for this subproject</b></summary>

![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png)

</details>

| ![SadMac_Tiny64px_HighCompression.png](SadMac_Tiny64px_HighCompression.png) Note: OpenHFS+ is no longer a default file system, but is still supported (as of WacOS 11.0) |
|-----------------------------------------------------------------------------------------------|

| [Previous (10.15)](https://github.com/seanpm2001/WacOS_10.15/) | [Current (11.x)](https://github.com/seanpm2001/WacOS_11/) | [Next (12.x)](https://github.com/seanpm2001/WacOS_12/) |
|---|---|---|
| ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) |
| First boot | First boot | First boot |
| ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) |
| Default boot | Default boot | Default boot |
| WacOS 10.15 (Catalina) [Local](/WacOS_X/10.15/) | WacOS 11.x (Big Sur) [Local](/WacOS_X/XI/11/) | WacOS 12.x (Monterey) [Local](/WacOS_X/XII/12/) |

# WacOS 11

WacOS 11/11.x is an open source recreation of MacOS 11.x (Big Sur) It is part of the WacOS operating system project. 

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

Source: [MacOS 11 - Wikipedia (en)](https://en.wikipedia.org/w/index.php?title=MacOS_Big_Sur&oldid=1088737685)

### Development history

Providing some indication as to how the pre-release operating system may have been viewed internally at Apple during its development cycle, documentation accompanying the initial beta release of macOS Big Sur referred to its version as "10.16", and when upgrading from prior versions of macOS using the Software Update mechanism to early beta releases, the version referred to was "10.16". An exception to this was the Developer Transition Kit, which always reported the system version as "11.0". macOS Big Sur started reporting the system version as "11.0" on all Macs as of the third beta release.

To maintain backwards compatibility, macOS Big Sur identified itself as 10.16 to software and in the browser user agent.

### System requirements

Unlike macOS Catalina, which supported every standard configuration Mac that Mojave supported, Big Sur drops support for various Macs released in 2012 and 2013. Big Sur runs on the following Macs:

- [x] MacBook: Early 2015 and newer
- [x] MacBook Air: Mid 2013 and newer
- [x] MacBook Pro: Late 2013 and newer
- [x] Mac Mini: Late 2014 and newer
- [x] iMac: Mid 2014 and newer
- [x] iMac Pro: Late 2017
- [x] Mac Pro: Late 2013 and newer
- [x] Developer Transition Kit (only up to Big Sur 11.3 beta 2)

### Changes

#### Design

macOS Big Sur refreshes the design of the user interface, described by Apple as the biggest change since the introduction of Mac OS X. Its changes include translucency in various places, a new abstract wallpaper for the first time and a new color palette. All standard apps, as well as the Dock and the Menu Bar, are redesigned and streamlined, and their icons now have rounded-square shapes like iOS and iPadOS apps. Compared to iOS, Big Sur's icons include more shading and highlights to give a three-dimensional appearance. Its aesthetic has been described as "neumorphism", a portmanteau of new and skeuomorphism. System sounds are redone as well.

The new OS also brings further integration with Apple's SF Symbols, enabling easier use by third-party developers as UI elements for their applications through AppKit, SwiftUI, and Catalyst, which makes it possible to unify third party applications with the existing Apple-made design language.

#### Interface

##### Control Center

An interface with quick toggles for Wi-Fi, Bluetooth, screen brightness and system volume has been added to the menu bar. This interface is functionally and visually similar to Control Center on iOS and iPadOS.

##### Notification Center

The Notification Center is redesigned, featuring interactive notifications and a transparent user interface. Notification Center also features a new widget system similar to that in iOS 14, displaying more information with more customization than previously available.
System

##### Support for Apple silicon

macOS Big Sur is the first release of macOS for Macs powered by Apple-designed ARM64-based processors, a key part of the transition from Intel x86-64-based processors. The chip mentioned in demo videos, and used in the Developer Transition Kit, is the A12Z Bionic. On November 10, 2020, Apple announced the first Mac Apple silicon chip, the Apple M1, in the Late 2020 Mac Mini, MacBook Air, and MacBook Pro. Apple has said that it will support Intel Macs "for years to come", and most software that has not been ported to run on ARM Macs can use Rosetta 2, an update of a compatibility mechanism originally developed for the PowerPC-to-Intel x86 transition. Likewise, Apple also introduced an updated universal binary format, Universal 2, which allows developers to package their applications so that they can run natively on both ARM64 and x86-64 processors.

##### Support for iOS and iPadOS applications

On Macs based on Apple silicon, macOS Big Sur can run iOS and iPadOS applications natively and without any modifications needed from developers, aside from allowing the app to be available on the Mac App Store. The first Macs with this capability are those that use the Apple M1 SoC (system on a chip).

##### Time Machine

Time Machine, the backup mechanism introduced in Mac OS X 10.5 Leopard, has been overhauled to utilize the APFS file system instead of HFS+. Specifically, the new version of Time Machine makes use of APFS's snapshot technology. According to Apple, this enables "faster, more compact, and more reliable backups" than were possible previously with HFS+-formatted backup destinations. An independent evaluation of this claim found that Time Machine on macOS 11 in conjunction with APFS was 2.75-fold faster upon initial local backup and 4-fold faster upon subsequent backups relative to macOS 10.15's Time Machine implementation using HFS+. A more modest yet nevertheless significant advantage was noted as well for backups to network-attached disks.

New local (i.e. USB- or Thunderbolt-connected) and network-connected Time Machine backup destinations are formatted as APFS by default, though Time Machine can continue backing up to existing HFS+ backup volumes. There is no option to convert existing, HFS+-based backups to APFS; instead, users who want to benefit from the advantages of the new, APFS-based implementation of Time Machine need to start with a fresh volume.

In the new version of Time Machine, encryption appears to be required (instead of merely optional) for local disks, but it remains elective for networked volumes.

It is no longer possible to restore the whole system using a Time Machine backup, as the signed system volume is not backed up. Non-core applications and user data can be restored in full using Migration Assistant, preceded by a system reinstall if necessary.

##### Spotlight

Spotlight, the file system indexing-and-search mechanism introduced in Mac OS X 10.4 Tiger, is faster and the interface has been refined. Spotlight is now the default search mechanism in Safari, Pages, and Keynote.

##### Signed system volume

The system volume containing the core operating system is cryptographically signed. Apple indicates this is a security measure to prevent malicious tampering. This includes adding an SHA-256 hash for every file on the system volume, preventing changes from third-party entities and the end user.

##### Software updates

Software updates can begin in the background before a restart, thus requiring less downtime to complete. Because system files are cryptographically signed, the update software can rely on them being in precise locations, thus permitting them to be effectively updated in place.

##### Encryption

macOS Big Sur supports encryption at the file level. Earlier versions of macOS (10.15 Catalina and older) supported encryption only at the level of entire volumes. As of June 2020, this capability is known to be compatible with Macs based on Apple silicon; it is unclear whether it is compatible with Intel-based Macs.

##### Other changes

- [x] Bilingual dictionaries in French–German, Indonesian–English, Japanese–Simplified Chinese and Polish–English
- [x] Better predictive input for Chinese and Japanese users
- [x] New fonts for Indian users
- [x] The "Now Playing" widget has been moved from the Notification Center to the Menu Bar
- [x] Podcasts "Listen Now" feature
- [x] FaceTime sign language prominence
- [x] Network Utility has been deprecated
- [x] macOS startup sound is enabled by default (it had been disabled by default on some machines in 2016), and an option in System Preferences was added to enable or disable this functionality.

#### Application features
	
##### Safari

Big Sur includes Safari 14, which was also released for macOS Catalina and macOS Mojave on September 16, 2020. Safari 14 includes features such as a new home page in which users can customize what features are visible in addition to being able to set a custom wallpaper. It also allows the viewer to preview a page and favicon before visiting it.

Safari 14 also includes built-in web page translations in English, Spanish, German, French, Russian, Chinese and Portuguese as well as support for 4K HDR content from Netflix on Macs with an Apple T2 chip, although none of these were made available for macOS Catalina and Mojave.

Privacy features such as iCloud Keychain (which notifies users of compromised passwords), extension privacy management and Privacy Report (which monitors privacy trackers and further increases Safari's security) were added for Safari 14. Users were now also able to import password from Google's Chrome browser in addition to being notified of compromised passwords.

Safari 14 also supports WebExtensions API, the WebP image format as well as VP9 decoding, the latter of which allows for the playback of 4K and HDR content from YouTube. In addition, it allowed for better performance and power efficiency.

Safari 14 ended support for Adobe Flash Player in September, 3 months prior to its end-of-life on December 31, 2020.

##### Messages

The Messages app was rewritten to be based upon Apple's Catalyst technology to enable it to have feature parity with its iOS counterpart. The new version of the app included a refined design as well as the ability to pin up to nine conversations that can sync across iOS, iPadOS and macOS. Users were also now allowed to search for messages and share their names and photos. Photo thumbnails could now also be used for group chats on the app.

In addition, users could mention contacts by putting the @ symbol in front of their name. They were also able to reply to specific messages. Memojis, 3d avatars were also made available on Messages. On Messages, users could now select photos based on parameters.

In India, text message effects were added when users sent certain texts (e.g., texting "Happy Holi" will result in users seeing effects).

##### App Store

The Mac App Store showing the Safari Extensions category

Refinements and new features of the Mac App Store include:

- [x] A new "nutrition label" section dedicated to the data and information an app collects, also featured in the iOS App Store
- [x] A new Safari extensions category
- [x] Third party Notification Center widgets, similar to those in iOS and iPadOS 14
- [x] The ability to share in-app purchases and subscriptions on the Mac via iCloud Family Sharing

##### Notes

- [x] Collapsible pinned section
- [x] Quick text style and formatting options
- [x] Scanning enhancements

##### Photos

- [x] New editing capabilities
- [x] Improved Retouch tool
- [x] New zooming feature in views

##### Maps

- [x] "Look Around" interactive street-level 360° panoramas, first implemented in the iOS 13 version of Maps, have been incorporated into the macOS version of Maps.
- [x] Availability of directions for cyclists.
- [x] Electric vehicle routing, based on proximity to charging stations and monitoring of battery levels (on selected car models).
- [x] Guides for exploring new places.

##### Voice Memos

- [x] a file structure has been implemented to allow organization of recordings in folders
- [x] recordings can be marked as Favorites for easier subsequent access
- [x] Smart Folders automatically group Apple Watch recordings, recently deleted recordings, and Favorites
- [x] audio can be enhanced to reduce background noise and room reverb

##### Other applications found in macOS 11 Big Sur

- [x] About This Mac
- [x] Activity Monitor
- [x] AirPort Utility
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
- [x] Directory Utility
- [x] Disk Utility
- [x] DVD Player
- [x] Expansion Slot Utility
- [x] FaceTime
- [x] Feedback Assistant
- [x] Find My
- [x] Finder
- [x] Folder Actions Setup
- [x] Font Book
- [x] Grapher
- [x] Home
- [x] Image Capture
- [x] iOS App Installer
- [x] Keychain Access
- [x] Mail
- [x] Launchpad
- [x] Migration Assistant
- [x] Mission Control
- [x] Music
- [x] Network Utility
- [x] News (only available for Australia, Canada, United Kingdom, and United States)
- [x] Photo Booth
- [x] Podcasts
- [x] Preview
- [x] QuickTime Player
- [x] Reminders
- [x] Screenshot (succeeded Grab since macOS 10.14 Mojave)
- [x] Script Editor
- [x] Siri
- [x] Stickies
- [x] Stocks
- [x] Storage Management
- [x] System Information
- [x] Terminal
- [x] TextEdit
- [x] Ticket Viewer
- [x] Time Machine
- [x] TV
- [x] VoiceOver Utility
- [x] Wireless Diagnostics

##### Removed functionality

- [x] Calculator Notification Center Widget
- [x] Option to toggle Font Smoothing in System Preferences
- [x] Support in Safari for AdBlock plugins like uBlock Origin
- [x] Removed the option to not have a clock in the menu bar.

### Criticism

The rollout of Big Sur came with several problems. Upgrading to the initial public release of Big Sur (version 11.0.1) bricked some computers, rendering them unusable. Many of these were 2013 and 2014 MacBook Pros, though problems were also observed on a 2019 MacBook Pro and an iMac from the same year. The initial rollout also disrupted Apple's app notarization process, causing slowdowns even on devices not running Big Sur. Users also reported that the update was slow or even might fail to install. macOS Catalina and Big Sur apps were taking a long time to load because of Gatekeeper issues.

The ongoing issues with the COVID-19 pandemic meant it was hard for users to visit an Apple Store to get their machines fixed. Shortly afterwards, Apple released a series of steps explaining how these Macs could be recovered.

Certain Apple applications running on early versions of Big Sur were reported to bypass firewalls, raising privacy and security concerns. This was addressed with the release of macOS Big Sur 11.2, which removed the whitelist for built-in programs. Conversely, security experts have reported that Big Sur will check an application's certificate every time it is run, degrading system performance. There have been reports that the operating system sends a hash back to Apple of every program run and when it was executed. Apple responded that the process is part of efforts to protect users from malware embedded in applications downloaded outside of the Mac App Store.

Some users have reported problems connecting external displays to Macs running Big Sur 11.1 and 11.2.

When upgrading Macs from 10.13, 10.14 and 10.15 to Big Sur the upgrade process could become stuck for seemingly unclear reasons. Only a full system restore from backup would solve this problem. On 21 October 2021 a solution became known that required removal of up to several hundreds of thousands excess temporary files in the system folders.

#### Vulnerability

In 2021, there were reports of two malware codes that infected macOS and include both x86-64 and ARM64 code. The first one was detected in early 2021. The second one, Silver Sparrow, was detected on nearly 30,000 Macs in February 2021.

### Boot screen

The `Happy Wac` is disabled by default on boot to match the release (starting with Wac OS X 10.2) and on boot, the letter `W` will show. By default, on the first install, it will go with the brand logo (The W logo with no detail) then it will be the `Modern1` logo (The W logo with a metal texture) This can be changed in [`WACOS_11_BOOTCONFIG.cfg`](/11.x/11.0/WACOS_11_BOOTCONFIG.cfg)

### File system
	
OpenAPFS is now the default file system (starting with WacOS 10.12)

OpenHFS+ 2.0 is no longer a file system option for the operating system (as of WacOS 11) but the file system itself is still supported, along with OpenZFS, which is still in read-only mode.

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

[Guesthouse repository](https://github.com/seanpm2001/WacOS_11/)

This is a guesthouse repository, and not a home repository, as development mainly stays on the main WacOS side. This is just the guesthouse that the project retreats to at times. If you are already in this repository, the link is likely recursive, and will reload the page.

[Home repository](https://github.com/seanpm2001/WacOS/tree/WacOS-dev/WacOS_X/XI/11/)

This is the home repository. If you are already in this repository, the link is likely recursive, and will reload the page.

***

## File info

**File type:** `Markdown document (*.md *.mkd *.mdown *.markdown)`

**File version:** `1 (2022, Wednesday, June 8th at 2:26 pm PST)`

**Line count (including blank lines and compiler line):** `406`

**Current article language:** `English (USA)`

***
