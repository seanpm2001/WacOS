  
***

# MacOS 10.14 (Mojave)

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.14_Mojave/MacOS_Mojave_wordmark.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.14_Mojave/MacOS_Mojave_wordmark.svg)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.14_Mojave/Mojave_desktop_marzipan.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.14_Mojave/Mojave_desktop_marzipan.jpg)

_As of 2021 September 24th, releases 10.14 (Mojave) and later are still supported and are receiving updates. The data here will need to be updated in the future._

( **Predecessor:** [MacOS 10.13 (High Sierra))](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-13-High-Sierra/) | **Successor:** [MacOS 10.15 (Catalina)](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Catalina/) )

macOS Mojave (/moʊˈhɑːvi, mə-/ mo-HAH-vee; version 10.14) is the fifteenth major release of macOS, Apple Inc.'s desktop operating system for Macintosh computers. Mojave was announced at Apple's Worldwide Developers Conference on June 4, [2018](https://github.com/seanpm2001/WacOS/wiki/2018/), and was released to the public on September 24, [2018](https://github.com/seanpm2001/WacOS/wiki/2018/). The operating system's name refers to the Mojave Desert and is part of a series of California-themed names that began with [OS X Mavericks](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-9-Mavericks/). It succeeded [macOS High Sierra](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-13-High-Sierra/) and was followed by [macOS Catalina](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Catalina/).

macOS Mojave brings several [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/) apps to the desktop operating system, including Apple News, Voice Memos, and Home. It also includes a much more comprehensive "dark mode", is the final version of macOS to support 32-bit application software, and is also the last version of macOS to support the iPhoto app, which had already been superseded in [OS X Yosemite (10.10)](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-10-Yosemite/) by the newer Photos app.

Mojave was well received and was supplemented by point releases after launch.

## Overview

macOS Mojave was announced on June 4, [2018](https://github.com/seanpm2001/WacOS/wiki/2018/), at Apple's annual Worldwide Developers Conference in San Jose, California. Apple pitched Mojave, named after the California desert, as adding "pro" features that would benefit all users. The developer preview of the operating system was released for developers the same day, followed by a public beta on June 26. The retail version of 10.14 was released on September 24. It was followed by several point updates and supplemental updates.

## System requirements

Mojave requires a GPU that supports Metal, and the list of compatible systems is more restrictive than the previous version, [macOS High Sierra](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-13-High-Sierra/). Compatible models are the following Macintosh computers running [OS X Mountain Lion](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-8-Mountain-Lion/) or later:

MacBook: Early 2015 or newer

MacBook Air: Mid 2012 or newer

MacBook Pro: Mid 2012 or newer, Retina display not needed

Mac Mini: Late 2012 or newer

iMac: Late 2012 or newer

iMac Pro: Late 2017

Mac Pro: Late 2013 or newer; Mid 2010 or Mid 2012 models require a Metal-capable GPU

macOS Mojave requires at least 2 GB of RAM as well as 12.5 GB of available disk space to upgrade from OS X El Capitan, macOS Sierra, or macOS High Sierra, or 18.5 GB of disk space to upgrade from OS X Yosemite and earlier releases. Some features are not available on all compatible models.

## Changes

### System updates

macOS Mojave deprecates support for several legacy features of the OS. The graphics frameworks OpenGL and OpenCL are still supported by the operating system, but will no longer be maintained; developers are encouraged to use Apple's Metal library instead.

OpenGL is a cross-platform graphics framework designed to support a wide range of processors. Apple chose OpenGL in the late 1990s to build support for software graphics rendering into the Mac, after abandoning QuickDraw 3D. At the time, moving to OpenGL allowed Apple to take advantage of existing libraries that enabled hardware acceleration on a variety of different GPUs. As time went on, Apple has shifted its efforts towards building its hardware platforms for mobile and desktop use. Metal makes use of the homogenized hardware by abandoning the abstraction layer and running on the "bare metal". Metal reduces CPU load, shifting more tasks to the GPU. It reduces driver overhead and improves multithreading, allowing every CPU thread to send commands to the GPU.

macOS does not natively support Vulkan, the Khronos group's official successor to OpenGL. The MoltenVK library can be used as a bridge, translating most of the Vulkan 1.0 API into the Metal API.

Continuing the process started in [macOS High Sierra (10.13)](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-13-High-Sierra/), which issued warnings about compatibility with 32-bit applications, Mojave issues warnings when opening 32-bit apps that they will not be supported in future updates. In macOS Mojave 10.14, this alert appears once every 30 days when launching the app, as macOS 10.15 will not support 32-bit applications.

When Mojave is installed, it will convert solid-state drives (SSDs), hard disk drives (HDDs), and Fusion Drives, from [HFS Plus](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/) to [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/). On Fusion Drives using [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/), files will be moved to the SSD based on the file's frequency of use and its SSD performance profile. [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/) will also store all metadata for a Fusion Drive's file system on the SSD.

New data protections require applications to get permission from the user before using the Mac camera and microphone or accessing system data like user Mail history and Messages database.

### Removed features

Mojave removes integration with Facebook, Twitter, Vimeo, and Flickr, which was added in [OS X Mountain Lion](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-8-Mountain-Lion/).

The only supported Nvidia graphics cards are the Quadro K5000 and GeForce GTX 680 Mac Edition.

### Applications

Mojave features changes to existing applications as well as new ones. Finder now has metadata preview accessed via View > Show Preview, and many other updates, including a Gallery View (replacing Cover Flow) that lets users browse through files visually. After a screenshot is taken, as with iOS, the image appears in the corner of the display. The screenshot software can now record video, choose where to save files, and be opened via ⇧ Shift + ⌘ Command + 5.

Safari's Tracking Prevention features now prevent social media "Like" or "Share" buttons and comment widgets from tracking users without permission. The browser also sends less information to web servers about the user's system, reducing the chance of being tracked based on system configuration. It can also automatically create, autofill, and store strong passwords when users create new online accounts; it also flags reused passwords so users can change them.

A new Screenshot app was added to macOS Mojave to replace the Grab app. Screenshot can capture a selected area, window or the entire screen as well as screen record a selected area or the entire display. The Screenshot app is located in the /Applications/Utilities/ folder, as was the Grab app. Screenshot can also be accessed by pressing ⇧ Shift+⌘ Command+3.

#### FaceTime

macOS 10.14.1, released on October 30, 2018, adds Group FaceTime, which lets users chat with up to 32 people at the same time, using video or audio from an iPhone, iPad or Mac, or audio from Apple Watch. Participants can join in mid-conversation.

#### App Store

The Mac App Store was rewritten from the ground up and features a new interface and editorial content, similar to the iOS App Store. A new 'Discover' tab highlights new and updated apps; Create, Work, Play and Develop tabs help users find apps for a specific project or purpose.

#### iOS apps ported to macOS

Four new apps (News, Stocks, Voice Memos and Home) are ported to macOS Mojave from [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/), with Apple implementing a subset of UIKit on the desktop OS. Third-party developers would be able to port iOS applications to macOS in 2019.

With Home, Mac users can control their HomeKit-enabled accessories to do things like turn lights off and on or adjust thermostat settings. Voice Memos lets users record audio (e.g., personal notes, lectures, meetings, interviews, or song ideas), and access them from iPhone, iPad or Mac. Stocks delivers curated market news alongside a personalized watchlist, with quotes and charts.

#### Other applications found on macOS 10.14 Mojave

Adobe Flash Player (installer)

AirPort Utility

Archive Utility

Audio MIDI Setup

Automator

Bluetooth File Exchange

Books

Boot Camp Assistant

Calculator

Calendar

Chess

ColorSync Utility

Console

Contacts

Dictionary

Digital Color Meter

Disk Utility

DVD Player

Font Book

GarageBand (may not be pre-installed)

Grab (still might be pre-installed)

Grapher

iMovie (may not be pre-installed)

iTunes

Image Capture

Ink (can only be accessed by connecting a graphics tablet into your Mac)

Keychain Access

Keynote (may not be pre-installed)

Mail

Migration Assistant

Notes, version 4.6

Numbers (may not be pre-installed)

Pages (may not be pre-installed)

Photo Booth

Preview

[QuickTime](https://github.com/seanpm2001/WacOS/wiki/QuickTime/) Player

Reminders

Screenshot (succeeded Grab since Mojave or Catalina)

Script Editor

Siri

Stickies

System Information

Terminal

[TextEdit](https://github.com/seanpm2001/WacOS/wiki/TextEdit/)

Time Machine

VoiceOver Utility

X11/XQuartz (may not be pre-installed)

### User interface

#### Dark mode and accent colors

Mojave introduces "Dark Mode", a Light-on-dark color scheme that darkens the user interface to make content stand out while the interface recedes. Users can choose dark or light mode when installing Mojave, or any time thereafter from System Preferences.

Apple's built-in apps support Dark Mode. App developers can implement Dark mode in their apps via a public API.

A limited dark mode that affected only the Dock, menu bar, and drop-down menus was previously introduced in [OS X Yosemite](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-10-Yosemite/).

#### Desktop

Stacks, a feature introduced in [Mac OS X Leopard](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-5-Leopard/), now lets users group desktop files into groups based on file attributes such as file kind, date last opened, date modified, date created, name and tags. This is accessed via View > Use Stacks.

macOS Mojave features a new Dynamic Desktop that automatically changes specially made desktop backgrounds (two of which are included) to match the time of day.
Dock

The [Dock](https://github.com/seanpm2001/WacOS/wiki/MacOS-Dock/) has a space for recently used apps that have not previously been added to the Dock.

### Preferences

macOS update functionality has been moved back to System Preferences from the Mac App Store. In [OS X Mountain Lion (10.8)](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-8-Mountain-Lion/), system and app updates moved to the App Store from Software Update.

## Reception

Mojave was generally well received by technology journalists and the press. The Verge's Jacob Kastrenakes considered Mojave a relatively minor update, but Kastrenakes and Jason Snell thought the release hinted at the future direction of macOS. In contrast, Ars Technica's Andrew Cunningham felt that "Mojave feels, if not totally transformative, at least more consequential than the last few macOS releases have felt." Cunningham highlighted productivity improvements and continued work on macOS's foundation.

TechCrunch’s Brian Heater dubbed Mojave "arguably the most focused macOS release in recent memory", playing an important role in reassuring professional users that it was still committed to them.

Mojave's new features were generally praised. Critics welcomed the addition of Dark Mode.

## Release history

**Previous release** 		**Current release**

Version 	Build 	Date 	Darwin 	Release Notes 	Standalone download

10.14 	18A391 	September 24, 2018 	18.0.0 	Original Mac App Store release

About the security content of macOS Mojave 10.14 	N/A

10.14.1 	18B75 	October 30, 2018 	18.2.0

xnu-4903.221.2~2 	About the macOS Mojave 10.14.1 Update

About the security content of macOS Mojave 10.14.1 	macOS Mojave 10.14.1 Update

18B2107 	October 30, 2018 		Added support for the new Vega GPUs in the MacBook Pro and MacBook Air 	

18B3094

10.14.2 	18C54 	December 5, 2018 	18.2.0

xnu-4903.231.4~2 	About the macOS Mojave 10.14.2 Update

About the security content of macOS Mojave 10.14.2 	macOS Mojave 10.14.2 Update

macOS Mojave 10.14.2 Combo Update

10.14.3 	18D42 	January 22, 2019 	18.2.0

xnu-4903.241.1~4 	About the macOS Mojave 10.14.3 Update

About the security content of macOS Mojave 10.14.3 	macOS Mojave 10.14.3 Update

macOS Mojave 10.14.3 Combo Update

18D43 	January 25, 2019

18D109 	February 7, 2019 	About the security content of macOS Mojave 10.14.3 Supplemental Update 	macOS Mojave 10.14.3 Supplemental Update

10.14.4 	18E226 	March 25, 2019 	18.5.0

xnu-4903.251.3~3 	About the macOS Mojave 10.14.4 Update

About the security content of macOS Mojave 10.14.4 	macOS Mojave 10.14.4 Update

macOS Mojave 10.14.4 Combo Update

18E227

10.14.5 	18F132 	May 13, 2019 	18.6.0

xnu-4903.261.4~2 	About the macOS Mojave 10.14.5 Update

About the security content of macOS Mojave 10.14.5 	macOS Mojave 10.14.5 Update

macOS Mojave 10.14.5 Combo Update

10.14.6 	18G84 	July 22, 2019 	18.7.0

xnu-4903.270.47~4 	About the macOS Mojave 10.14.6 Update

About the security content of macOS Mojave 10.14.6 	macOS Mojave 10.14.6 Update

macOS Mojave 10.14.6 Combo Update

18G87 	August 1, 2019 	Addressed the Wake from Sleep bug, which caused Macs not waking from sleep properly. 	macOS Mojave 10.14.6 Supplemental Update

18G95 	August 26, 2019 	18.7.0

xnu-4903.271.2~2 	Security updates and bug fixes

Addressed: MacBooks shutting down during sleep

Addressed: Performance throttling when handling large files

Addressed: iLife applications (Pages, Keynote, Numbers, GarageBand, and iMovie) not updating

Re-patched a vulnerability that was accidentally unpatched in the previous update, which could lead to hacking attempts

macOS Mojave 10.14.6 Supplemental Update

18G103 	September 26, 2019 	Security updates and bug fixes 	macOS Mojave 10.14.6 Supplemental Update

18G1012 	October 29, 2019 	18.7.0

xnu-4903.278.12~4 	About the security content of Security Update 2019-001 	Security Update 2019-001 (Mojave)

18G2022 	December 10, 2019 	18.7.0

xnu-4903.278.19~1 	About the security content of Security Update 2019-002 	Security Update 2019-002 (Mojave)

18G3020 	January 28, 2020 	18.7.0

xnu-4903.278.25~1 	About the security content of Security Update 2020-001 	Security Update 2020-001 (Mojave)

18G4032 	March 24, 2020 	18.7.0

xnu-4903.278.28~1 	About the security content of Security Update 2020-002 	Security Update 2020-002 (Mojave)

18G5033 	May 26, 2020 	18.7.0

xnu-4903.278.35~1 	About the security content of Security Update 2020-003 	Security Update 2020-003 (Mojave)

18G6020 	July 15, 2020 	18.7.0

xnu-4903.278.43~1 	About the security content of Security Update 2020-004 	Security Update 2020-004 (Mojave)

18G6032 	September 24, 2020 	18.7.0

xnu-4903.278.44~1

Pulled 2020-09-30 	About the security content of Security Update 2020-005 	Security Update 2020-005 (Mojave)

18G6032 	October 1, 2020 	18.7.0

xnu-4903.278.44~1

About the security content of macOS 10.14.6 Supplemental Update 	Security Update 2020-005 (Mojave)

18G6042 	November 12, 2020 	18.7.0

xnu-4903.278.44.0.2~1

About the security content of Security Update 2020-006 	Security Update 2020-006 (Mojave)

18G7016 	December 14, 2020 	18.7.0

xnu-4903.278.51~1

About the security content of Security Update 2020-007 	Security Update 2020-007 (Mojave)

18G8012 	February 1, 2021 	18.7.0

xnu-4903.278.56~1

About the security content of Security Update 2021-001 	Security Update 2021-001 (Mojave)

18G8022 	February 9, 2021 	About the security content of Security Update 2021-002 	Security Update 2021-002 (Mojave)

18G9028 	April 26, 2021 	18.7.0

xnu-4903.278.65~1

About the security content of Security Update 2021-003 	Security Update 2021-003 (Mojave)

18G9216 	May 24, 2021 	18.7.0

xnu-4903.278.68~1

About the security content of Security Update 2021-004 	Security Update 2021-004 (Mojave)

18G9323 	July 21, 2021 	18.7.0

xnu-4903.278.70~1

About the security content of Security Update 2021-005 	Security Update 2021-005 (Mojave)

**This article is a modified copy of the Wikipedia article of the same subject. It needs to be rewritten to be more original.**

***

## Sources

[Wikipedia - MacOS 10.14 (Mojave)](https://en.wikipedia.org/wiki/MacOS_Mojave)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Friday September 24th at 4:13 pm`

**Last revised on:** `2021 Friday September 24th at 4:13 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Friday September 24th at 4:13 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
