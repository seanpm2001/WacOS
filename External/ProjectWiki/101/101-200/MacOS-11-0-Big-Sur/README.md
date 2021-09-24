  
***

# MacOS 11.0 (Big Sur)

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_Big_Sur_wordmark_2.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_Big_Sur_wordmark_2.svg)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_Big_Sur_Beta_Desktop.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_Big_Sur_Beta_Desktop.png)

_As of 2021 September 24th, releases 10.14 (Mojave) and later are still supported and are receiving updates. The data here will need to be updated in the future._

( **Predecessor:** [MacOS 10.15 (Catalina))](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Catalina/) | **Successor:** [MacOS 12 (Monterey)](https://github.com/seanpm2001/WacOS/wiki/MacOS-12-0-Monterey/) )

macOS Big Sur (version 11) is the 17th and current major release of macOS, Apple Inc.'s operating system for Macintosh computers. It was announced at Apple's Worldwide Developers Conference (WWDC) on June 22, [2020](https://github.com/seanpm2001/WacOS/wiki/2020/),and was released to the public on [November 12, 2020](https://github.com/seanpm2001/WacOS/wiki/2020/).

Big Sur is the successor to [macOS Catalina](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Catalina/), and will be succeeded by [macOS Monterey](https://github.com/seanpm2001/WacOS/wiki/MacOS-12-0-Monterey/), currently scheduled for general release in fall [2021](https://github.com/seanpm2001/WacOS/wiki/2021/).

Most notably, macOS Big Sur features a user interface redesign that features new blurs to establish a visual hierarchy and also includes a revamp of the Time Machine backup mechanism, among other changes. It is also the first macOS version to support Macs with ARM-based processors. To mark the transition, the operating system's major version number was incremented, for the first time since 2000, from [10](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/) to 11. The operating system is named after the coastal region of Big Sur in the Central Coast of California.

## Development history

Providing some indication as to how the pre-release operating system may have been viewed internally at Apple during its development cycle, documentation accompanying the initial beta release of macOS Big Sur referred to its version as "10.16", and when upgrading from prior versions of macOS using the Software Update mechanism to early beta releases, the version referred to was "10.16". An exception to this was the Developer Transition Kit, which always reported the system version as "11.0". macOS Big Sur started reporting the system version as "11.0" on all Macs as of the third beta release.

To maintain backwards compatibility, macOS Big Sur identified itself as 10.16 to software and in the browser user agent.

## System requirements

Unlike [macOS Catalina](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Cataina/), which supported every standard configuration Mac that Mojave supported, Big Sur drops support for various Macs released in 2012 and 2013. Big Sur runs on the following Macs:

MacBook: Early 2015 and newer

MacBook Air: Mid 2013 and newer

MacBook Pro: Late 2013 and newer

Mac Mini: Late 2014 and newer

iMac: Mid 2014 and newer

iMac Pro: Late 2017

Mac Pro: Late 2013 and newer

Developer Transition Kit (only up to Big Sur 11.3 beta 2)

## Changes

### Design

macOS Big Sur refreshes the design of the user interface, described by Apple as the biggest change since the introduction of [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/). Its changes include translucency in various places, a new abstract wallpaper for the first time and a new color palette. All standard apps, as well as the [Dock](https://github.com/seanpm2001/WacOS/wiki/MacOS-Dock/) and the Menu Bar, are redesigned and streamlined, and their icons now have rounded-square shapes like [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/) and [iPadOS](https://github.com/seanpm2001/WacOS/wiki/iPadOS/) apps. Compared to [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/), Big Sur's icons include more shading and highlights to give a three-dimensional appearance. Its aesthetic has been described as "neumorphism", a portmanteau of new and skeuomorphism. System sounds are redone as well.

The new OS also brings further integration with Apple's SF Symbols, enabling easier use by third-party developers as UI elements for their applications through AppKit, SwiftUI, and Catalyst, which makes it possible to unify third party applications with the existing Apple-made design language.

#### Interface

##### Control Center

An interface with quick toggles for Wi-Fi, Bluetooth, screen brightness and system volume has been added to the menu bar.[14] This interface is functionally and visually similar to Control Center on iOS and iPadOS.

##### Notification Center

The Notification Center is redesigned, featuring interactive notifications and a transparent user interface. Notification Center also features a new widget system similar to that in iOS 14, displaying more information with more customization than previously available.[6]

### System

#### Support for Apple silicon

macOS Big Sur is the first release of macOS for Macs powered by Apple-designed ARM64-based processors, a key part of the transition from Intel x86-64-based processors. The chip mentioned in demo videos, and used in the Developer Transition Kit, is the A12Z Bionic. On November 10, 2020, Apple announced the first Mac Apple silicon chip, the Apple M1, in the Late 2020 Mac Mini, MacBook Air, and MacBook Pro. Apple has said that it will support Intel Macs "for years to come", and most software that has not been ported to run on ARM Macs can use Rosetta 2, an update of a compatibility mechanism originally developed for the PowerPC-to-Intel x86 transition. Likewise, Apple also introduced an updated universal binary format, Universal 2, which allows developers to package their applications so that they can run natively on both ARM64 and x86-64 processors.

#### Support for iOS and iPadOS applications

On Macs based on Apple silicon, macOS Big Sur can run iOS and iPadOS applications natively and without any modifications needed from developers, aside from allowing the app to be available on the Mac App Store. The first Macs with this capability are those that use the Apple M1 SoC (system on a chip).

#### Time Machine

Time Machine, the backup mechanism introduced in [Mac OS X 10.5 Leopard](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-5-Leopard/), has been overhauled to utilize the [APFS file system](https://github.com/seanpm2001/WacOS/wiki/APFS/) instead of [HFS+](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/). Specifically, the new version of Time Machine makes use of APFS's snapshot technology.] According to Apple, this enables "faster, more compact, and more reliable backups" than were possible previously with [HFS+](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/)-formatted backup destinations. An independent evaluation of this claim found that Time Machine on macOS 11 in conjunction with [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/) was 2.75-fold faster upon initial local backup and 4-fold faster upon subsequent backups relative to [macOS 10.15's](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Cataina/) Time Machine implementation using [HFS+](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/). A more modest yet nevertheless significant advantage was noted as well for backups to network-attached disks.

New local (i.e. USB- or Thunderbolt-connected) and network-connected Time Machine backup destinations are formatted as [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/) by default, though Time Machine can continue backing up to existing [HFS+](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/) backup volumes. There is no option to convert existing, [HFS+](https://github.com/seanpm2001/WacOS/wiki/HFS-Plus/)-based backups to [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/); instead, users who want to benefit from the advantages of the new, [APFS](https://github.com/seanpm2001/WacOS/wiki/APFS/)-based implementation of Time Machine need to start with a fresh volume.

In the new version of Time Machine, encryption appears to be required (instead of merely optional) for local disks, but it remains elective for networked volumes.

It is no longer possible to restore the whole system using a Time Machine backup, as the signed system volume is not backed up. Non-core applications and user data can be restored in full using Migration Assistant, preceded by a system reinstall if necessary.

#### Spotlight

Spotlight, the file system indexing-and-search mechanism introduced in [Mac OS X 10.4 Tiger](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-4-Tiger/), is faster and the interface has been refined. Spotlight is now the default search mechanism in Safari, Pages, and Keynote.

#### Signed system volume

The system volume containing the core operating system is cryptographically signed. Apple indicates this is a security measure to prevent malicious tampering. This includes adding an SHA-256 hash for every file on the system volume, preventing changes from third-party entities and the end user.

#### Software updates

Software updates can begin in the background before a restart, thus requiring less downtime to complete. Because system files are cryptographically signed, the update software can rely on them being in precise locations, thus permitting them to be effectively updated in place.
Encryption

macOS Big Sur supports encryption at the file level. Earlier versions of macOS ([10.15 Catalina](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Catalima/) and older) supported encryption only at the level of entire volumes. As of June [2020](https://github.com/seanpm2001/WacOS/wiki/2020/), this capability is known to be compatible with Macs based on Apple silicon; it is unclear whether it is compatible with Intel-based Macs.

## Other changes

Bilingual dictionaries in French–German, Indonesian–English, Japanese–Simplified Chinese and Polish–English

Better predictive input for Chinese and Japanese users

New fonts for Indian users

The "Now Playing" widget has been moved from the Notification Center to the Menu Bar

Podcasts "Listen Now" feature

FaceTime sign language prominence

Network Utility has been deprecated

macOS startup sound is enabled by default (it had been disabled by default on some machines in 2016), and an option in System Preferences was added to enable or disable this functionality.

### Application features

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_11.0_Beta_-_Safari_14_homepage.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_11.0_Beta_-_Safari_14_homepage.png)

The Safari 14 start page with Wikipedia on the reading list

#### Safari

Big Sur includes Safari 14, released for [macOS Catalina](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-15-Catalina/) and [macOS Mojave](https://github.com/seanpm2001/WacOS/wiki/MacOS-10-14-Mojave/) on September 16, [2020](https://github.com/seanpm2001/WacOS/wiki/2020/). Safari 14 includes:

a new customizable home page with the ability to personalize what features are visible and set a custom wallpaper

improved tab design with page previews and favicons

Built-in web page translation in English, Spanish, German, French, Russian, Chinese and Portuguese. The feature is currently in beta and will not be available in macOS Catalina and Mojave.

new privacy features such as Privacy Report, which monitors privacy trackers and further increases Safari's security

iCloud Keychain password monitoring, which notifies the user of compromised passwords

better performance and power efficiency

extension privacy management

support for WebExtensions API

the ability to import passwords from Google's Chrome browser

support for 4K HDR content from Netflix on Macs with an Apple T2 chip

support for VP9 decoding, allowing for playback of 4K and HDR content from YouTube

support for WebP image format

The new version of Safari also officially ends support for Adobe Flash Player, 3 months ahead of its end-of-life and 10 years after Steve Jobs' "Thoughts on Flash".

#### Messages

The Messages app was rewritten to be based upon Apple's Catalyst technology. This enables the app to have feature parity with its [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/) counterpart. Alongside a refined design, the new Messages app brings:

Conversation pinning for up to nine conversations that sync across [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/), [iPadOS](https://github.com/seanpm2001/WacOS/wiki/iPadOS/), and macOS

Message searching

Name and photo sharing

Group chat photo thumbnails

@Mentioning individuals by name

Inline message replies

Memoji stickers and editor

A new photo picker

Localized message effects for users in India

#### App Store

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_Big_Sur_-_Safari_Extensions_category_in_App_Store.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_11/MacOS_Big_Sur_-_Safari_Extensions_category_in_App_Store.jpg)

The Mac App Store showing the Safari Extensions category

Refinements and new features of the Mac App Store include:

A new "nutrition label" section dedicated to the data and information an app collects, also featured in the iOS App Store

A new Safari extensions category

Third party Notification Center widgets, similar to those in iOS and iPadOS 14

The ability to share in-app purchases and subscriptions on the Mac via iCloud Family Sharing

#### Notes

Collapsible pinned section

Quick text style and formatting options

Scanning enhancements

#### Photos

New editing capabilities

Improved Retouch tool

New zooming feature in views

#### Maps

"Look Around" interactive street-level 360° panoramas, first implemented in the [iOS 13](https://github.com/seanpm2001/WacOS/wiki/iOS-13/) version of Maps, have been incorporated into the macOS version of Maps.

Availability of directions for cyclists.

Electric vehicle routing, based on proximity to charging stations and monitoring of battery levels (on selected car models).

Guides for exploring new places.

#### Voice Memos

A file structure has been implemented to allow organization of recordings in folders

recordings can be marked as Favorites for easier subsequent access

Smart Folders automatically group Apple Watch recordings, recently deleted recordings, and Favorites

audio can be enhanced to reduce background noise and room reverb

#### Other applications found in macOS 11 Big Sur

About This Mac

Activity Monitor

AirPort Utility

Archive Utility

Audio MIDI Setup

Automator

Bluetooth File Exchange

Books

Boot Camp Assistant (may not be pre-installed)

Calculator

Calendar

Chess

ColorSync Utility

Console

Contacts

Dictionary

Digital Color Meter

Directory Utility

Disk Utility

DVD Player

Expansion Slot Utility

FaceTime

Feedback Assistant

Find My

Finder

Folder Actions Setup

Font Book

GarageBand (may not be pre-installed)

Grapher

Home

iMovie (may not be pre-installed)

Image Capture

iOS App Installer

Keychain Access

Keynote (may not be pre-installed)

Mail

Launchpad

Migration Assistant

Mission Control

Music

Network Utility

News (only available for Australia, Canada, United Kingdom, and United States)

Numbers (may not be pre-installed)

Pages (may not be pre-installed)
  
Photo Booth

Podcasts

Preview

[QuickTime](https://github.com/seanpm2001/WacOS/wiki/QuickTime/) Player

Reminders

Screen Sharing

Screenshot (succeeded Grab since macOS 10.14 Mojave)

Script Editor

Siri

Stickies

Stocks

Storage Management

System Information

Terminal

[TextEdit](https://github.com/seanpm2001/WacOS/wiki/TextEdit/)

Ticket Viewer

Time Machine

TV

VoiceOver Utility

Wireless Diagnostics

#### Removed functionality

Calculator Notification Center Widget

Support for Adobe Flash Player

Option to toggle Font Smoothing in System Preferences

## Criticism

The rollout of Big Sur came with several problems. Upgrading to the initial public release of Big Sur (version 11.0.1) bricked some computers, rendering them unusable. Many of these were 2013 and 2014 MacBook Pros, though problems were also observed on a 2019 MacBook Pro and an iMac from the same year. The initial rollout also disrupted Apple's app notarization process, causing slowdowns even on devices not running Big Sur. Users also reported that the update was slow or even might fail to install. macOS Catalina and Big Sur apps were taking a long time to load because of Gatekeeper issues.

The ongoing issues with the COVID-19 pandemic meant it was hard for users to visit an Apple Store to get their machines fixed. Shortly afterwards, Apple released a series of steps explaining how these Macs could be recovered.

Certain Apple applications running on early versions of Big Sur were reported to bypass firewalls, raising privacy and security concerns. This was addressed with the release of macOS Big Sur 11.2, which removed the whitelist for built-in programs. Conversely, security experts have reported that Big Sur will check an application's certificate every time it is run, degrading system performance. There have been reports that the operating system sends a hash back to Apple of every program run and when it was executed. Apple responded that the process is part of efforts to protect users from malware embedded in applications downloaded outside of the Mac App Store.

Some users have reported problems connecting external displays to Macs running Big Sur 11.1 and 11.2. Later versions 11.5.2 also reported to have display issues when multiple monitors are connected.

## Vulnerability

In [2021](https://github.com/seanpm2001/WacOS/wiki/2021/), there were reports of two malware codes that infected macOS and include both x86-64 and ARM64 code. The first one was detected in early [2021](https://github.com/seanpm2001/WacOS/wiki/2021/). The second one, Silver Sparrow, was detected on nearly 30,000 Macs in February [2021](https://github.com/seanpm2001/WacOS/wiki/2021/).

## Release history

The public release of macOS 11 Big Sur began with 11.0.1 for Intel Macs. Version 11.0 was preinstalled on Apple silicon Macs, and Apple advised those with that version to be updated to 11.0.1.

**Previous release** 		**Current release** 		**Current developer release**

Version 	Build 	Date 	Darwin version 	Release notes 	Notes

11.0 	20A2411 	November 17, 2020 	20.1.0

xnu-7195.41.8~9 	N/A 	Preinstalled on MacBook Air (M1, 2020), MacBook Pro (13-inch, M1, 2020), and Mac mini (M1, 2020)

11.0.1 	20B29 	November 12, 2020 	20.1.0

xnu-7195.50.7~2 	Security content 	Initial public release

20B50 	November 19, 2020 		Available for all Macs except Late 2013 and Mid 2014 13" MacBook Pros

11.1 	20C69 	December 14, 2020 	20.2.0

xnu-7195.60.75~1 	Release notes

Security content
	
11.2 	20D64 	February 1, 2021 	20.3.0

xnu-7195.81.3~1 	Release notes

Security content
	
11.2.1 	20D74 	February 9, 2021 	Security content 	

20D75 	February 15, 2021 	Fixes bug where installation with insufficient free space could cause data loss - exclusive to the full installer[58]

11.2.2 	20D80 	February 25, 2021 	N/A 	

11.2.3 	20D91 	March 8, 2021 	Security content 	

11.3 	20E232 	April 26, 2021 	20.4.0

xnu-7195.101.1~3 	Release notes

Security content
	
11.3.1 	20E241 	May 3, 2021 	20.4.0

xnu-7195.101.2~1 	Security content 	

11.4 	20F71 	May 24, 2021 	20.5.0

xnu-7195.121.3~9 	Release notes

Security content
	
11.5 	20G71 	July 21, 2021 	20.6.0

xnu-7195.141.2~5

Wed Jun 23 00:26:31 PDT 2021 	Release notes

Security content
	
11.5.1 	20G80 	July 26, 2021 	Release notes

Security content
	
11.5.2 	20G95 	August 11, 2021 	Release notes 	

11.6 	20G165 	September 13, 2021 	20.6.0

xnu-7195.141.6~3

Mon Aug 30 06:12:21 PDT 2021 	Release notes

Security content	

**This article is a modified copy of the Wikipedia article of the same subject. It needs to be rewritten to be more original.**

***

## Sources

[Wikipedia - MacOS 11.0 (Big Sur)](https://en.wikipedia.org/wiki/MacOS_Big_Sur)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Friday September 24th at 3:36 pm`

**Last revised on:** `2021 Friday September 24th at 3:36 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Friday September 24th at 3:36 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
