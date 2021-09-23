  
***

# Rhapsody (Operating system)

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

( **Predecessor:** `None` | **Successor:** [Mac OS X Server 1.0)](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-Server-1-0/) )


Rhapsody was the code name given to Apple Computer's next-generation operating system during the period of its development between Apple's purchase of [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) in late [1996](https://github.com/seanpm2001/WacOS/wiki/1996/) and the announcement of [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/) (now called "macOS") in [1998](https://github.com/seanpm2001/WacOS/wiki/1998/). At first more than an operating system, Rhapsody represented a new strategy for Apple, who intended the operating system to run on x86-based PCs and DEC Alpha workstations as well as on [PowerPC](https://github.com/seanpm2001/WacOS/wiki/PowerPC/)-based Macintosh hardware. In addition, the underlying API frameworks would be ported to run natively on Microsoft Windows NT. Eventually, the non-Apple platforms were dropped, and later versions consisted primarily of the [OPENSTEP](https://github.com/seanpm2001/WacOS/wiki/OPENSTEP/) operating system ported to the Power Macintosh, along with a new GUI to make it appear more Mac-like. Several existing "classic" Mac OS technologies were also ported to Rhapsody, including [QuickTime](https://github.com/seanpm2001/WacOS/wiki/QuickTime/) and AppleSearch. Rhapsody could also run [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) in a "Blue Box" emulation layer.

## History

Rhapsody was announced at the MacWorld Expo in San Francisco on January 7, [1997](https://github.com/seanpm2001/WacOS/wiki/1997/) and first demonstrated at the 1997 Worldwide Developers Conference (WWDC). There were two subsequent general Developer Releases for computers with x86 or [PowerPC](https://github.com/seanpm2001/WacOS/wiki/1997/) processors. After this there was to be a "Premier" version somewhat analogous to the [Mac OS X Public Beta](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-Public-Beta/), followed by the full "Unified" version in the second quarter of [1998](https://github.com/seanpm2001/WacOS/wiki/1998/). Apple's development schedule in integrating the features of two very different systems made it difficult to forecast the features of upcoming releases. At the [1998](https://github.com/seanpm2001/WacOS/wiki/1998/) MacWorld Expo in New York, Steve Jobs announced that Rhapsody would be released as [Mac OS X Server 1.0](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-Server-1-0/) (which shipped in [1999](https://github.com/seanpm2001/WacOS/wiki/1999/)). No home version of Rhapsody would be released. Its code base was forked into [Darwin](https://github.com/seanpm2001/WacOS/wiki/Darwin/), the open source underpinnings of macOS.

## Design

Defining features of the Rhapsody operating system included a heavily modified "hybrid" OSFMK 7.3 (Open Software Foundation Mach Kernel) from the OSF, a BSD operating system layer (based on 4.4BSD), the object-oriented Yellow Box API framework, the Blue Box compatibility environment for running "classic" Mac OS applications, and a Java Virtual Machine.

The user interface was modeled after [Mac OS 8's](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) "Platinum" appearance. The file management functions served by the Finder in previous Mac OS versions were instead handled by a port of OPENSTEP's Workspace Manager. Additional features inherited from [OPENSTEP](https://github.com/seanpm2001/WacOS/wiki/OPENSTEP/) and not found in the classic Mac OS Finder were included, such as the Shelf and column view. Although the Shelf was dropped in favor of Dock functionality, column view would later make its way to macOS's Finder.

Rhapsody's Blue Box environment, available only when running on the [PowerPC](https://github.com/seanpm2001/WacOS/wiki/PowerPC/) architecture, was responsible for providing runtime compatibility with existing Mac OS applications. Compared to the more streamlined and integrated Classic compatibility layer that was later featured in [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/), Blue Box's interface presented users with a distinct barrier between emulated legacy software and native Rhapsody applications. All emulated applications and their associated windows were encapsulated within a single Blue Box emulation window instead of being interspersed with the other applications using the native Yellow Box API. This limited cross-environment interoperability and caused various user interface inconsistencies.

To avoid the pitfalls of running within the emulation environment and take full advantage of Rhapsody's features, software needed to be rewritten to use the new Yellow Box API. Inherited from [OPENSTEP](https://github.com/seanpm2001/WacOS/wiki/OPENSTEP/), Yellow Box used an object-oriented model completely unlike the procedural model used by the Classic APIs. The large difference between the two frameworks meant transition of legacy code required significant changes and effort on the part of the developer. The consequent lack of adoption as well as objections by prominent figures in the Macintosh software market, including Adobe Systems and Microsoft, became major factors in Apple's decision to cancel the Rhapsody project in [1998](https://github.com/seanpm2001/WacOS/wiki/1998/).

However, most of Yellow Box and other Rhapsody technologies went on to be used in macOS's Cocoa API. Bowing to developers' wishes, Apple also ported existing Classic Mac OS technologies into the new operating system and implemented the Carbon API to provide Classic Mac OS API compatibility. Widely used Mac OS libraries like QuickTime and AppleScript were ported and made available to developers. Carbon allowed developers to maintain full compatibility and native functionality using their current codebases, while enabling them to take advantage of new features at their discretion.

## Name

The name Rhapsody followed a pattern of music-related code names that Apple designated for operating system releases during the 1990s. Another next-generation operating system, which was to be the successor to the never-completed [Copland](https://github.com/seanpm2001/WacOS/wiki/Copland/) operating system, was code-named Gershwin after George Gershwin, composer of Rhapsody in Blue. Copland itself was named after another American composer, Aaron Copland. Other musical code names include Harmony ([Mac OS 7.6](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-7/)), Tempo (Mac OS 8), Allegro ([Mac OS 8.5](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/)), and Sonata ([Mac OS 9](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-9/)).

## Release history

**Unsupported**

Version 	Code name 	Date 	OS name 	Platform

Rhapsody Developer Release 	Grail1Z4 	1997-08-31 	Rhapsody 5.0 	IA-32, PowerPC

Rhapsody Developer Release 2 	Titan1U 	1998-05-14 	Rhapsody 5.1

Rhapsody Premier 		1998 	Rhapsody 5.2 	PowerPC

[Mac OS X Server 1.0](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-Server-1-0/) 	Hera1O9 	1999-03-16 	Rhapsody 5.3

Mac OS X Server 1.0.1 	1999-04-15 	Rhapsody 5.4

Mac OS X Server 1.0.2 	Hera1O9+Loki2G1 	1999-07-29 	Rhapsody 5.5

Mac OS X Server 1.2 	Pele1Q10 	2000-01-14 	Rhapsody 5.6

Mac OS X Server 1.2 v3 	Medusa1E3 	2000-10-27

**This article on classic Mac OS is a stub. You can help by expanding it.**

**This article is a modified copy of the Wikipedia article of the same subject. It needs to be rewritten to be more original.**

***

## Sources

[Wikipedia - Rhapsody (operating system)](https://en.wikipedia.org/wiki/Rhapsody_(operating_system))

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Thursday September 23rd at 4:23 pm`

**Last revised on:** `2021 Thursday September 23rd at 4:23 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021 Thursday September 23rd at 4:23 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
