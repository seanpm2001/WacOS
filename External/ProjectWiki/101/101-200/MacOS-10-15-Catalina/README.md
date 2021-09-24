  
***

# MacOS 10.15 (Catalina)

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.15_Catalina/MacOS_Catalina_wordmark.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.15_Catalina/MacOS_Catalina_wordmark.svg)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.15_Catalina/MacOS_Catalina_Desktop.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_10/10.15_Catalina/MacOS_Catalina_Desktop.png)

_As of 2021 September 24th, releases 10.14 (Mojave) and later are still supported and are receiving updates. The data here will need to be updated in the future._

( **Predecessor:** [MacOS 10.14 (Mojave))](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/) | **Successor:** [MacOS 11.0 (Big Sur)](https://github.com/seanpm2001/WacOS/wiki/MacOS-11-0-Big-Sur/) )

macOS Catalina (version 10.15) is the sixteenth major release of macOS, Apple Inc.'s desktop operating system for Macintosh computers. It is the successor to [macOS Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/) and was announced at WWDC 2019 on June 3, [2019](https://github.com/seanpm2001/WacOS/wiki/2019/) and released to the public on October 7, [2019](https://github.com/seanpm2001/WacOS/wiki/2019/). Catalina is the first version of macOS to support only 64-bit applications and the first to include Activation Lock. It is also the last version of macOS to have the version number prefix of 10. Its successor, [Big Sur](https://github.com/seanpm2001/WacOS/wiki/MacOS-11-0-Big-sur/), is version 11. [macOS Big Sur](https://github.com/seanpm2001/WacOS/wiki/MacOS-11-0-Big-sur/), released on November 12, 2020, succeeded macOS Catalina.

The operating system is named after Santa Catalina Island, which is located off the coast of southern California.

## System requirements

macOS Catalina officially runs on all standard configuration Macs that supported [Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/). 2010–2012 Mac Pros, which could run [Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/) only with a GPU upgrade, are no longer supported. Catalina requires 4 GB of memory, an increase over the 2 GB required by [Lion](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-7-Lion/) through [Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/).

iMac: Late 2012 or newer

iMac Pro: Late 2017

Mac Pro: Late 2013 or newer

Mac Mini: Late 2012 or newer

MacBook: Early 2015 or newer

MacBook Air: Mid 2012 or newer

MacBook Pro: Mid 2012 or newer, Retina display not needed

It is possible to install Catalina on many older Macintosh computers that are not officially supported by Apple. This requires using a patch to modify the install image.

## Applications

AirPort Utlity

App Store

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

FaceTime

Find My

Font Book

GarageBand (may not be pre-installed)

Grapher

Home

iMovie (may not be pre-installed)

Image Capture

Keychain Access

Keynote (may not be pre-installed)

Mail

Migration Assistant

Music

News (only available for Australia, Canada, United Kingdom, and United States)

Notes

Numbers (may not be pre-installed)

Pages (may not be pre-installed)

Photo Booth

Podcasts

Preview

[QuickTime](https://github.com/seanpm2001/WacOS/wiki/QuickTime/) Player

Reminders

Screenshot (succeeded Grab since Mojave)

Script Editor

Stickies

Stocks

System Information

Terminal

[TextEdit](https://github.com/seanpm2001/WacOS/wiki/TextEdit/)

Time Machine

TV

Voice Memos

VoiceOver Utility

X11/XQuartz (may not be pre-installed)

## Changes

### System

#### Catalyst

Catalyst is a new software-development tool that allows developers to write apps that can run on both macOS and [iPadOS](https://github.com/seanpm2001/WacOS/wiki/iPadOS/). Apple demonstrated several ported apps, including Jira and Twitter (after the latter discontinued its macOS app in February 2018).

#### System extensions

An upgrade from Kexts. System extensions avoid the problems of Kexts. There are 3 kinds of System extensions: Network Extensions, Endpoint Security Extensions, and Driver Extensions. System extensions run in userspace, outside of the kernel. Catalina will be the last version of macOS to support legacy system extensions.

#### DriverKit

A replacement for IOKit device drivers, driver extensions are built using DriverKit. DriverKit is a new SDK with all-new frameworks based on IOKit, but updated and modernized. It is designed for building device drivers in userspace, outside of the kernel.

#### Gatekeeper

Mac apps, installer packages, and kernel extensions that are signed with a Developer ID must be notarized by Apple to run on macOS Catalina.

#### Activation Lock

Activation Lock helps prevent the unauthorized use and drive erasure of devices with an Apple T2 security chip (2018, 2019, and 2020 MacBook Pro; 2020 5K iMac; 2018 MacBook Air, iMac Pro; 2018 Mac Mini; 2019 Mac Pro).

#### Dedicated system volume

The system runs on its own read-only volume, separate from all other data on the Mac.

#### Voice control

Users can give detailed voice commands to applications. On-device machine processing is used to offer better navigation.

#### Sidecar

Sidecar allows a Mac to use an iPad (running [iPadOS](https://github.com/seanpm2001/WacOS/wiki/iPadOS/)) as a wireless external display. With Apple Pencil, the device can also be used as a graphics tablet for software running on the computer. Sidecar requires a Mac with Intel Skylake CPUs and newer (such as the fourth-generation MacBook Pro), and an iPad that supports Apple Pencil.

#### Support for wireless game controllers

The Game Controller framework adds support for two major console game controllers: the PlayStation 4's DualShock 4 and the Xbox One controller.

#### Time Machine

A number of under-the-hood changes were made to Time Machine, the backup software. For example, the manner in which backup data is stored on network-attached devices was changed, and this change is not backwards-compatible with earlier versions of macOS. Apple declined to document these changes, but some of them have been noted.

## Applications

### iTunes

iTunes is replaced by separate Music, Podcasts, TV and Books apps, in line with [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/). [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/) device management is now conducted via Finder. The TV app on Mac supports Dolby Atmos, Dolby Vision, and HDR10 on MacBooks released in 2018 or later, while 4K HDR playback is supported on Macs released in 2018 or later when connected to a compatible display.

An older version of iTunes can still be installed and will work separately from the new apps (according to support information on Apple’s website).

### Find My

Find My Mac and Find My Friends are merged into an application called Find My.

### Notes

The Notes application was enhanced to allow better management of checklists and the ability to share folders with other users. The application version was incremented from 4.6 (in [macOS 10.14 Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/)) to 4.7.

#### Reminders

Among other visual and functional overhauls, attachments can be added to reminders and Siri can intelligently estimate when to remind the user about an event.
Voice Memos

The Voice Memos application, first ported from iOS to the Mac in [macOS 10.14 Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/) as version 2.0, was incremented to version 2.1.

### Removed or changed components

macOS Catalina exclusively supports 64-bit applications. 32-bit applications no longer run (including all software that utilizes the Carbon API as well as QuickTime 7 applications, image, audio and video codecs). Apple has also removed all 32-bit-only apps from the Mac App Store.

Zsh is the default login shell and interactive shell in macOS Catalina, replacing Bash, the default shell since [Mac OS X Panther](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-3-Panther/) in 2003. Bash continues to be available in macOS Catalina, along with other shells such as csh/tcsh and ksh.

Dashboard has been removed in macOS Catalina.

The ability to add Backgrounds in Photo Booth was removed in macOS Catalina.

The command-line interface GNU Emacs application was removed in macOS Catalina.

Built-in support for Perl, [Python 2.7](https://github.com/seanpm2001/WacOS/wiki/Python/) and Ruby are included in macOS for compatibility with legacy software. Future versions of macOS will not include scripting language runtimes by default, possibly requiring users to install additional packages.

Legacy AirDrop for connecting with Macs running [Mac OS X Lion](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-7-Lion/), [Mountain Lion](https://github.com/seanpm2001/WacOS/wiki/OS-X-10-8-Mountain-Lion/) and [Mavericks](https://github.com/seanpm2001/WacOS/wiki/Mavericks/), or 2011 and older Macs has been removed.

## Reception

Catalina received favourable reviews on release for some of its features. However, some critics found the OS version distinctly less reliable than earlier versions. The broad addition of user-facing security measures (somewhat analogous to the addition of User Account Control dialog boxes with Windows Vista a decade earlier) was criticised as intrusive and annoying.

## Release history

**Previous release** 		**Current release**

Version 	Build 	Date 	Darwin 	Release Notes 	Standalone download

10.15 	19A583 	October 7, 2019 	19.0.0 	Original Software Update release

Security content
	
19A602 	October 15, 2019 	Supplemental update 	

19A603 	October 21, 2019 	Revised Supplemental update 	

10.15.1 	19B88 	October 29, 2019 	19.0.0

xnu-6153.41.3~29 	About the macOS Catalina 10.15.1 Update

Security content

macOS 10.15.1 Update

10.15.2 	19C57 & 19C58 	December 10, 2019 	19.2.0

xnu-6153.61.1~20 	About the macOS Catalina 10.15.2 Update

Security content

macOS 10.15.2 Update

macOS 10.15.2 Combo Update

10.15.3 	19D76 	January 28, 2020 	19.3.0

xnu-6153.81.5~1 	About the macOS Catalina 10.15.3 Update

Security content

macOS 10.15.3 Update

macOS 10.15.3 Combo Update

10.15.4 	19E266 	March 24, 2020 	19.4.0

xnu-6153.101.6~15 	About the macOS Catalina 10.15.4 Update

Security content

macOS 10.15.4 Update

macOS 10.15.4 Combo Update

19E287 	April 8, 2020 	Supplemental update 	macOS 10.15.4 Supplemental Update

10.15.5 	19F96 	May 26, 2020 	19.5.0

xnu-6153.121.1~7 	About the macOS Catalina 10.15.5 Update

Security content

macOS 10.15.5 Update

macOS 10.15.5 Combo Update

19F101 	June 1, 2020 	19.5.0

xnu-6153.121.2~2 	Supplemental update

Security content

macOS 10.15.5 Supplemental Update

10.15.6 	19G73 	July 15, 2020 	19.6.0

xnu-6153.141.1~9

Jul 5 00:43:10 PDT 2020 	About the macOS Catalina 10.15.6 Update

Security content

macOS 10.15.6 Update

macOS 10.15.6 Combo Update

19G2021 	August 12, 2020 	19.6.0

xnu-6153.141.1~1

Jun 18 20:49:00 PDT 2020 	Supplemental update 	macOS 10.15.6 Supplemental Update

10.15.7 	19H2 	September 24, 2020 	19.6.0

xnu-6153.141.2~1

Mon Aug 31 22:12:52 PDT 2020 	About the macOS Catalina 10.15.7 Update

Security content

macOS 10.15.7 Update

macOS 10.15.7 Combo Update

19H4 	October 27, 2020

19H15 	November 5, 2020 	19.6.0

xnu-6153.141.2.2~1

Thu Oct 29 22:56:45 PDT 2020 	Supplemental update

Security content

macOS 10.15.7 Supplemental Update

macOS 10.15.7 Supplemental Update (Combo)

19H114 	December 14, 2020 	19.6.0

xnu-6153.141.10~1

Tue Nov 10 00:10:30 PST 2020 	About the security content of Security Update 2020-001 	Security Update 2020-001 (Catalina)

19H512 	February 1, 2021 	19.6.0

xnu-6153.141.16~1

Tue Jan 12 22:13:05 PST 2021 	About the security content of Security Update 2021-001 	Security Update 2021-001 (Catalina)

19H524 	February 9, 2021 	Supplemental Update

Security content

macOS Catalina 10.15.7 Supplemental Update 2

19H1030 	April 26, 2021 	19.6.0

xnu-6153.141.28.1~1

Mon Apr 12 20:57:45 PDT 2021 	About the security content of Security Update 2021-002 	Security Update 2021-002 (Catalina)

19H1217 	May 24, 2021 	19.6.0

xnu-6153.141.33~1

Thu May 6 00:48:39 PDT 2021 	About the security content of Security Update 2021-003 	Security Update 2021-003 (Catalina)

19H1323 	July 21, 2021 	19.6.0

xnu-6153.141.35~1

Thu Jun 22 19:49:55 PDT 2021 	About the security content of Security Update 2021-004 	Security Update 2021-004 (Catalina)

19H1417 	September 13, 2021 	19.6.0

xnu-6153.141.40~1

Tue Aug 24 20:28:00 PDT 2021 	About the security content of Security Update 2021-005 	Security Update 2021-005 (Catalina)

19H1419 	September 23, 2021 	19.6.0

xnu-6153.141.40.1~1

Thu Sep 16 20:58:47 PDT 2021 	About the security content of Security Update 2021-006 	Security Update 2021-006 (Catalina)

**This article is a modified copy of the Wikipedia article of the same subject. It needs to be rewritten to be more original.**

***

## Sources

[Wikipedia - MacOS 10.15 (Catalina)](https://en.wikipedia.org/wiki/MacOS_Catalina)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Friday September 24th at 3:57 pm`

**Last revised on:** `2021 Friday September 24th at 3:57 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Friday September 24th at 3:57 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
