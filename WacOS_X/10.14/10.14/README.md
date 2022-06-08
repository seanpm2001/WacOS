
***

<details open><summary><b lang="en">Click/tap here to expand/collapse the logo for this subproject</b></summary>

![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png)

</details>

| ![SadMac_Tiny64px_HighCompression.png](SadMac_Tiny64px_HighCompression.png) Note: 32 bit programs will no longer compatible in WacOS 10.15 and up |
|-----------------------------------------------------------------------------------------------|

| [Previous (10.13)](https://github.com/seanpm2001/WacOS_10.13/) | [Current (10.14)](https://github.com/seanpm2001/WacOS_10.14/) | [Next (10.15)](https://github.com/seanpm2001/WacOS_10.15/) |
|---|---|---|
| ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) | ![/W_Plain_HighCompression.png](/W_Plain_HighCompression.png) |
| First boot | First boot | First boot |
| ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) | ![/W_Modern1_HighCompression](/W_Modern1_HighCompression.png) |
| Default boot | Default boot | Default boot |
| WacOS 10.13 (High Sierra) [Local](/WacOS_X/10.13/) | WacOS 10.14 (Mojave) [Local](/WacOS_X/10.14/) | WacOS 10.15 (Catalina) [Local](/WacOS_X/10.15/) |

# WacOS 10.14

WacOS 10.14 is an open source recreation of MacOS 10.14 (Mojave) It is part of the WacOS operating system project. 

## Language

The system is currently written in C, but will also support several other languages, including x86 Assembly, Objective-C, Objective-C++, Swift, Metal, and AppleScript

### Programming language support

Objective-C is bumped up to version 2.0 (As of Wac OS X 10.6)

Python version 2.5 is included (As of Wac OS X 10.5)

Ruby 1.8.6 is included (As of Wac OS X 10.5)

Swift is now supported (Starting with WOAHS X 10.9)

Metal is now supported (Starting with WacOS 10.12)

Objective-C++ is now supported (as of WacOS 10.12)

## Features

Features to replicate

Source: [MacOS 10.14 - Wikipedia (en)(https://en.wikipedia.org/w/index.php?title=MacOS_Mojave&oldid=1086549164)

macOS Mojave was announced on June 4, 2018, at Apple's annual Worldwide Developers Conference in San Jose, California. Apple pitched Mojave, named after the California desert, as adding "pro" features that would benefit all users. The developer preview of the operating system was released for developers the same day, followed by a public beta on June 26. The retail version of 10.14 was released on September 24, 2018.

### System requirements

Mojave requires a GPU that supports Metal, and the list of compatible systems is more restrictive than the previous version, macOS High Sierra. Compatible models are the following Macintosh computers running OS X Mountain Lion or later:

- [x] MacBook: Early 2015 or newer
- [x] MacBook Air: Mid 2012 or newer
- [x] MacBook Pro: Mid 2012 or newer, Retina display not needed
- [x] Mac Mini: Late 2012 or newer
- [x] iMac: Late 2012 or newer
- [x] iMac Pro: Late 2017
- [x] Mac Pro: Late 2013 or newer; Mid 2010 or Mid 2012 models require a Metal-capable GPU

macOS Mojave requires at least 2 GB of RAM as well as 12.5 GB of available disk space to upgrade from OS X El Capitan, macOS Sierra, or macOS High Sierra, or 18.5 GB of disk space to upgrade from OS X Yosemite and earlier releases. Some features are not available on all compatible models. Mojave installations convert the installation volume to Apple File System (APFS), if the volume had not previously been converted from HFS+.

### Changes

#### System updates

macOS Mojave deprecates support for several legacy features of the OS. The graphics frameworks OpenGL and OpenCL are still supported by the operating system, but will no longer be maintained; developers are encouraged to use Apple's Metal library instead.

OpenGL is a cross-platform graphics framework designed to support a wide range of processors. Apple chose OpenGL in the late 1990s to build support for software graphics rendering into the Mac, after abandoning QuickDraw 3D. At the time, moving to OpenGL allowed Apple to take advantage of existing libraries that enabled hardware acceleration on a variety of different GPUs. As time went on, Apple has shifted its efforts towards building its hardware platforms for mobile and desktop use. Metal makes use of the homogenized hardware by abandoning the abstraction layer and running on the "bare metal". Metal reduces CPU load, shifting more tasks to the GPU. It reduces driver overhead and improves multithreading, allowing every CPU thread to send commands to the GPU.

macOS does not natively support Vulkan, the Khronos group's official successor to OpenGL. The MoltenVK library can be used as a bridge, translating most of the Vulkan 1.0 API into the Metal API.

Continuing the process started in macOS High Sierra (10.13), which issued warnings about compatibility with 32-bit applications, Mojave issues warnings when opening 32-bit apps that they will not be supported in future updates. In macOS Mojave 10.14, this alert appears once every 30 days when launching the app, as macOS 10.15 will not support 32-bit applications.

When Mojave is installed, it will convert solid-state drives (SSDs), hard disk drives (HDDs), and Fusion Drives, from HFS Plus to APFS. On Fusion Drives using APFS, files will be moved to the SSD based on the file's frequency of use and its SSD performance profile. APFS will also store all metadata for a Fusion Drive's file system on the SSD.

New data protections require applications to get permission from the user before using the Mac camera and microphone or accessing system data like user Mail history and Messages database.

#### Removed features

Mojave removes integration with Facebook, Twitter, Vimeo, and Flickr, which was added in OS X Mountain Lion.

The only supported Nvidia graphics cards are the Quadro K5000 and GeForce GTX 680 Mac Edition.

### Applications

Mojave features changes to existing applications as well as new ones. Finder now has metadata preview accessed via View > Show Preview, and many other updates, including a Gallery View (replacing Cover Flow) that lets users browse through files visually. After a screenshot is taken, as with iOS, the image appears in the corner of the display. The screenshot software can now record video, choose where to save files, and be opened via ⇧ Shift + ⌘ Command + 5.

Safari's Tracking Prevention features now prevent social media "Like" or "Share" buttons and comment widgets from tracking users without permission. The browser also sends less information to web servers about the user's system, reducing the chance of being tracked based on system configuration. It can also automatically create, autofill, and store strong passwords when users create new online accounts; it also flags reused passwords so users can change them.

A new Screenshot app was added to macOS Mojave to replace the Grab app. Screenshot can capture a selected area, window or the entire screen as well as screen record a selected area or the entire display. The Screenshot app is located in the /Applications/Utilities/ folder, as was the Grab app. Screenshot can also be accessed by pressing ⇧ Shift+⌘ Command+3.

#### FaceTime

macOS 10.14.1, released on October 30, 2018, adds Group FaceTime, which lets users chat with up to 32 people at the same time, using video or audio from an iPhone, iPad or Mac, or audio from Apple Watch. Participants can join in mid-conversation.

#### App Store

The Mac App Store was rewritten from the ground up and features a new interface and editorial content, similar to the iOS App Store. A new 'Discover' tab highlights new and updated apps; Create, Work, Play and Develop tabs help users find apps for a specific project or purpose.

####  iOS apps ported to macOS

Four new apps (News, Stocks, Voice Memos and Home) are ported to macOS Mojave from iOS, with Apple implementing a subset of UIKit on the desktop OS. Third-party developers would be able to port iOS applications to macOS in 2019.

With Home, Mac users can control their HomeKit-enabled accessories to do things like turn lights off and on or adjust thermostat settings. Voice Memos lets users record audio (e.g., personal notes, lectures, meetings, interviews, or song ideas), and access them from iPhone, iPad or Mac. Stocks delivers curated market news alongside a personalized watchlist, with quotes and charts.

#### Other applications found on macOS 10.14 Mojave

- [x] Adobe Flash Player (installer)
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
- [x] Disk Utility
- [x] DVD Player
- [x] Font Book
- [x] GarageBand (may not be pre-installed)
- [x] Grab (still might be pre-installed)
- [x] Grapher
- [x] iMovie (may not be pre-installed)
- [x] iTunes
- [x] Image Capture
- [x] Ink (can only be accessed by connecting a graphics tablet into your Mac)
- [x] Keychain Access
- [x] Keynote (may not be pre-installed)
- [x] Mail
- [x] Migration Assistant
- [x] Notes, version 4.6
- [x] Numbers (may not be pre-installed)
- [x] Pages (may not be pre-installed)
- [x] Photo Booth
- [x] Preview
- [x] QuickTime Player
- [x] Reminders
- [x] Screenshot (succeeded Grab since Mojave or Catalina)
- [x] Script Editor
- [x] Siri
- [x] Stickies
- [x] System Information
- [x] Terminal
- [x] TextEdit
- [x] Time Machine
- [x] VoiceOver Utility
- [x] X11/XQuartz (may not be pre-installed)

### User interface

#### Dark mode and accent colors

Mojave introduces "Dark Mode", a Light-on-dark color scheme that darkens the user interface to make content stand out while the interface recedes. Users can choose dark or light mode when installing Mojave, or any time thereafter from System Preferences.

Apple's built-in apps support Dark Mode. App developers can implement Dark mode in their apps via a public API.

A limited dark mode that affected only the Dock, menu bar, and drop-down menus was previously introduced in OS X Yosemite.

#### Desktop

Stacks, a feature introduced in Mac OS X Leopard, now lets users group desktop files into groups based on file attributes such as file kind, date last opened, date modified, date created, name and tags. This is accessed via View > Use Stacks.

macOS Mojave features a new Dynamic Desktop that automatically changes specially made desktop backgrounds (two of which are included) to match the time of day.

#### Dock

The Dock has a space for recently used apps that have not previously been added to the Dock.

#### Preferences

macOS update functionality has been moved back to System Preferences from the Mac App Store. In OS X Mountain Lion (10.8), system and app updates moved to the App Store from Software Update.

### Reception

Mojave was generally well received by technology press. The Verge's Jacob Kastrenakes considered Mojave a relatively minor update and typical of 2010s macOS releases, but Kastrenakes and Jason Snell thought the release hinted at the future direction of macOS. In contrast, Ars Technica's Andrew Cunningham felt that "Mojave feels, if not totally transformative, at least more consequential than the last few macOS releases have felt." Cunningham highlighted productivity improvements and continued work on macOS's foundation. TechCrunch's Brian Heater dubbed Mojave "arguably the most focused macOS release in recent memory", playing an important role in reassuring professional users that it was still committed to them.

Mojave's new features were generally praised. Critics welcomed the addition of Dark Mode, although some noted that its effect was inconsistent; MacWorld's Karen Haslam noted that it did not affect the bright white background in Pages, for instance. Others noted that Dark Mode's utility was curtailed by the lack of third-party developer support at release.
Release history

### Boot screen

The `Happy Wac` is disabled by default on boot to match the release (starting with Wac OS X 10.2) and on boot, the letter `W` will show. By default, on the first install, it will go with the brand logo (The W logo with no detail) then it will be the `Modern1` logo (The W logo with a metal texture) This can be changed in [`WACOS_10-14-BOOTCONFIG.cfg`](/10.14/WACOS_10-14_BOOTCONFIG.cfg)

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

[Guesthouse repository](https://github.com/seanpm2001/WacOS_10.14/)

This is a guesthouse repository, and not a home repository, as development mainly stays on the main WacOS side. This is just the guesthouse that the project retreats to at times. If you are already in this repository, the link is likely recursive, and will reload the page.

[Home repository](https://github.com/seanpm2001/WacOS/tree/WacOS-dev/WacOS_X/10.14/)

This is the home repository. If you are already in this repository, the link is likely recursive, and will reload the page.

***

## File info

**File type:** `Markdown document (*.md *.mkd *.mdown *.markdown)`

**File version:** `1 (2022, Wednesday, June 8th at 2:29 am PST)`

**Line count (including blank lines and compiler line):** `319`

**Current article language:** `English (USA)`

***
