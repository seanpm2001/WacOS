  
***

# Mac OS 7

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Classic-Mac-OS/7/Mac_OS_7.6.1_emulated_inside_of_SheepShaver.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Classic-Mac-OS/7/Mac_OS_7.6.1_emulated_inside_of_SheepShaver.png)

( **Predecessor:** [System 6 (1988)](https://github.com/seanpm2001/WacOS/wiki/Apple-System-6/) | **Successor:** [Mac OS 8 (1997)](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) )

Mac OS 7 is the seventh major version of the classic MacOS series of operating systems produced by Apple Inc.

System 7, codenamed "Big Bang", and also known as Mac OS 7, is a graphical user interface-based operating system for Macintosh computers and is part of the classic Mac OS series of operating systems. It was introduced on May 13, [1991](https://github.com/seanpm2001/WacOS/wiki/1991/), by Apple Computer, Inc. It succeeded [System 6](https://github.com/seanpm2001/WacOS/wiki/Apple-System-6/), and was the main Macintosh operating system until it was succeeded by [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) in [1997](https://github.com/seanpm2001/WacOS/wiki/1997/). Features added with the System 7 release included virtual memory, personal file sharing, QuickTime, QuickDraw 3D, and an improved user interface.

With the release of version 7.6 in 1997, Apple officially renamed the operating system "Mac OS", a name which had first appeared on System 7.5.1's boot screen. System 7 was developed for Macs that used the Motorola 680x0 line of processors, but was ported to the [PowerPC](https://github.com/seanpm2001/WacOS/wiki/POWERpc/) after Apple adopted the new processor in [1994](https://github.com/seanpm2001/WacOS/wiki/1994/) with the introduction of the Power Macintosh.

## Development

The development of the Macintosh system software up to [System 6](https://github.com/seanpm2001/WacOS/wiki/Apple-System-6) followed a fairly smooth progression with the addition of new features and relatively small changes and upgrades over time. Major additions were fairly limited. Some perspective on the scope of the changes can be seen by examining the official system documentation, Inside Macintosh. This initially shipped in three volumes, adding another to describe the changes introduced with the Mac Plus, and another for the Mac II and Mac SE.

These limited changes meant that the original Macintosh system remained largely as it was when initially introduced. That is, the machine was geared towards a single user and task running on a floppy disk based machine of extremely limited RAM. However, many of the assumptions of this model were no longer appropriate. Most notable among these was the single-tasking model, the replacement of which had first been examined in [1986's](https://github.com/seanpm2001/WacOS/wiki/1986/) "Switcher" and then replaced outright with MultiFinder in [System 5](https://github.com/seanpm2001/WacOS/wiki/Apple-System-5/). Running MultiFinder normally required a larger amount of RAM and a hard drive, but these became more common by the late 1980s.

While additions had been relatively limited, so had fixes to some of the underlying oddities of the system architecture. For instance, to support a limited form of multitasking, the original Mac OS supported small co-resident programs known as desk accessories which had to be installed into the system using special tools. If the system were able to support multiple tasks, this one-off solution would no longer be needed — desk accessories could simply be small programs, placed anywhere. Yet, as MultiFinder was still optional, such a step had not been taken. Numerous examples of this sort of problem could be found throughout the system.

Finally, the widespread adoption of hard drives and local area networks led to any number of new features being requested from users and developers. By the late 1980s, the list of new upgrades and suggested changes to the existing model was considerable.
Pink and Blue

In March 1988, shortly before the release of System 6, technical middle managers at Apple held an offsite meeting to plan the future course of Mac OS development. Ideas were written on index cards; features that seemed simple enough to implement in the short term (like adding color to the user interface) were written on blue cards, longer-term goals like true multitasking on pink cards, and "far out" ideas like an object-oriented file system on red cards. Development of the ideas contained on the blue and pink cards was to proceed in parallel, and at first the two projects were known simply as "blue" and "pink" (including [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/)). Apple intended to have the "blue" team (which came to call themselves the "Blue Meanies" after characters in Yellow Submarine) release an updated version of the existing Macintosh operating system in the 1990–1991 time-frame, and the "pink" team to release an entirely new OS around 1993.

As Blue was aimed at relatively "simple" upgrades, the feature list reads to some degree as a sort of "[System 6](https://github.com/seanpm2001/WacOS/wiki/Apple-System-6/), corrected". In the underlying OS, a number of formerly optional components were made mandatory:

32-bit QuickDraw, supporting so-called "true color" imaging, was included as standard; it was previously available as a system extension.

A new Sound Manager API, version 2.0, replaced the older ad hoc APIs. The new APIs featured significantly improved hardware abstraction, as well as higher-quality playback. Although technically not a new feature for System 7 (as these features were available for System 6.0.7), Sound Manager 2.0 was the first widespread implementation of this technology to make it to most Mac users.

System 7 paved the way for a full 32-bit address space, from the previous 24-bit address space. This process involved making all of the routines in OS code use the full 32 bits of a pointer as an address—prior systems used the upper bits as flags. This change was known as being "32-bit clean". While System 7 itself was 32-bit clean, many existing machines and thousands of applications were not, so it was some time before the process was completed. To ease the transition, the "Memory" control panel contained a switch to disable this feature, allowing for compatibility with older applications but rendering any RAM over 8 MB unusable.

System 7 made MultiFinder's co-operative multitasking mandatory.

Furthermore, a number of oddities in the original System, typically included due to limited resources, were finally changed to use basic underlying OS features:

Trash was now a normal directory, allowing items to be preserved between reboots and disk eject events instead of being purged.

"System extensions" (small pieces of INIT code that extended the system's functionality) were relocated to their own subfolder (rather than in the root level of the System Folder itself as on earlier versions) and they could be installed or removed at the user's will simply by moving these "extensions" to or from the folder and then rebooting the computer. There was an auto-routing feature for extensions, control panels, fonts and Desk Accessories where they could simply be dropped onto the System folder. The system would detect the type and automatically place the moved files in the appropriate subdirectories. On reboot, the System would read the files and install the extensions, without the user having to do anything else. Additionally, all extensions and panels (see below) could be temporarily disabled by holding down the shift key when booting up. Later versions of System 7 offered a feature called "Extensions Manager" which simplified the process of enabling/disabling individual extensions. Extensions were often a source of instability and these changes made them more manageable and assisted trouble-shooting.

Similarly, the Control Panel desk accessory became the Control Panels folder (found in the System Folder, and accessible to the user from an alias in the Apple menu). The control panels themselves became separate files, stored within this directory. Control panels are essentially system extensions with a user interface.

The Apple menu (previously home only to desk accessories pulled from "DRVR" resources in the System file) now listed the contents of a folder ("Apple Menu Items"), including aliases (see below). Desk accessories had originally been intended to provide a form of multitasking and were no longer necessary now that real multitasking was always enabled. The desk-accessory technology was deprecated, with System 7 treating them largely the same as other applications. Desk accessories now ran in their own process rather than borrowing that of a host application.

Under [System 6](https://github.com/seanpm2001/WacOS/wiki/Apple-System-6/), the Apple Menu contained both a list of desk accessories, as well as a list of running programs under MultiFinder. In System 7 the list of active programs was re-located to its own Application Menu.

The system also offered a wide variety of new features:

Personal File Sharing. Along with various UI improvements for AppleTalk setup, System 7 also included a basic file sharing server allowing any machine to publish folders to the AppleTalk network.

Aliases. An alias is a small file that represents another object in the file system. A typical alias is small, between 1 and 5 KB. Similar in concept to Unix symbolic links and Windows shortcuts, an alias acts as a redirect to any object in the file system, such as a document, an application, a folder, a hard disk, a network share or removable medium or a printer. When double-clicked, the computer will act the same way as if the original file had been double-clicked. Likewise, choosing an alias file from within an "Open" dialog box would open the original file. (Unlike the path-based approach of shortcuts and symbolic links, aliases also store a reference to the file's catalog entry, so they continue to work even if the file is moved or renamed. Aliases have features of both hard links and symbolic links found on Unix-based systems. All three are supported on macOS.)

Drag and drop. Document icons could be dragged with the mouse and "dropped" onto application icons to open in the targeted application. Under System 6, one either double-clicked on a document icon to open its associated application, or one could open the desired application and use its Open dialog box. The development of the drag-and-drop paradigm led to a new concept for some applications—such as StuffIt Expander—whose main interactions were intended to be via drag and drop. System 7.5's Drag Manager expanded the concept system-wide to include multiple data types such as text or audio data.

"Stationery", a template feature that allowed users to save often-used document styles in special format. "Stationery-aware" applications would create a new, untitled file containing the template data, while non-aware applications would immediately show a Save As dialog box asking the user for the file's name.

Balloon Help, a widget-identification system similar to tooltips.

[AppleScript](https://github.com/seanpm2001/WacOS/wiki/AppleScript/), a scripting language for automating tasks. While fairly complex for application programmers to implement support for, this feature was powerful and popular with users, and it remains supported as part of macOS.

AppleEvents. Supporting AppleScript was a new interprocess communication model for "high-level" events to be sent into applications, along with support to allow this to take place over an AppleTalk network.

Publish and Subscribe. This feature permitted data "published" by one application to be imported ("subscribed to") by another, and the data could be updated dynamically. Programmers complained that the API was unwieldy, and relatively few applications ended up adopting it.

TrueType outline fonts. Up to this point, all fonts on the Macintosh were bitmapped, or a set of bitmapped screen fonts paired with outline PostScript printer fonts; TrueType for the first time offered a single font format that scaled to any size on screen and on paper. This technology was recognized as being so important that a TrueType extension for System 6 was also released, along with an updated Font/DA Mover capable of installing these new kinds of fonts into the System 6 System file.

A newly colorized user interface. Although this feature made for a visually appealing interface, it was optional. On machines not capable of displaying color, or those with their display preferences set to monochrome, the interface defaulted back to the black-and-white look of previous versions. Only some widgets were colorized—scrollbars, for instance, had a new look, but buttons remained in black and white.

System 7.1 marked the advent of System Enablers, small extensions that were loaded at startup to support Macintosh models introduced since the last OS revision. Under [System 6](https://github.com/seanpm2001/WacOS/wiki/Apple-System-6/), Apple had to introduce a number of minor revisions to the OS solely for use with new hardware. Apple introduced an unprecedented number of new Macintosh models during the System 7 era, leading to some confusion over which System Enabler went with which computer(s).

## Software

System 7 was the first Apple operating system to be available on compact disc, although it shipped on a set of 15 floppy disks initially. Unlike earlier systems, System 7 did not come bundled with major software packages. Newly purchased Macintosh computers had System 7 installed and were often bundled with software such as HyperCard, At Ease and Mouse Practice. Later, the Macintosh Performa family added various software bundles including ClarisWorks, The New Grolier Multimedia Encyclopedia, Microsoft Bookshelf, Spectre VR and Power Pete. Since System 7 was introduced before the Internet came to popular attention, software such as MacTCP, FreePPP and Netscape were not included at first, but was later available on disk from Internet service providers and bundled with books such as Adam C. Engst's Internet Starter Kit for Macintosh. Power Macintosh machines also included NuCalc, a graphing calculator. System 7 also includes AppleTalk networking and file sharing software in the form of system extensions and control panels.

The basic utilities installed by default with System 7 include TeachText (which was replaced by SimpleText in later versions) for basic text editing tasks and reading readme documents. Also available on the additional "Disk Tools" floppy disk are Disk First Aid for disk repair and Apple HD SC Setup for initializing and partitioning disks.

Later versions of System 7, specifically System 7.5 and Mac OS 7.6, come with a dedicated "Utilities" folder and "Apple Extras" folder including: AppleScript, Disk Copy, QuickDraw GX Extras and QuickTime Movie Player. More optional extras and utilities could be manually installed from the System CD.

## Transition to PowerPC

System 7.1.2 is the first version of the Macintosh System Software to support Apple's new PowerPC-based computers. 68k applications which had not yet been updated to run natively on these systems were emulated transparently (without the user having to intervene) by a built-in 68k processor emulator. Fat binaries, which contained the code necessary to run natively on both PowerPC and 68k systems, became common during this time. This process was similar to the distribution of universal binaries during the Mac transition to Intel processors in 2006, as well as the Mac transition to Apple silicon beginning in [2020](https://github.com/seanpm2001/WacOS/wiki/2020/).

## PC compatibility

System 7.0 through 7.1 offered a utility called Apple File Exchange, which could access the contents of FAT- and Apple II-formatted floppy disks. System 7 Pro, System 7.5 and up shipped with PC Exchange, previously a separate product, which allowed the system to mount FAT-formatted floppy disks on the desktop in the same manner as regular Macintosh disks.

OS/2 disks were read as PC DOS disks, due to fact that OS/2 used the FAT file system. At this time, Macs could also read and write UNIX file systems with the help of extra software.

System 7 allowed users to access PC networks and allowed communication via TCP/IP and other compatible networking stacks. Actual PC software compatibility, however, required third party software such as SoftPC, which allowed some MS-DOS and early Microsoft Windows programs to run, or Connectix Virtual PC, which allowed the Mac to run Windows via full PC emulation.

Other PC compatibility solutions took a more native approach by running Windows and MS-DOS by using x86 expansion cards with an x86 chip on the card. Apple offered some systems configured this way, marketed as "DOS Compatible"—a card with dedicated x86 CPU and RAM was used, while the Mac hard drive, sound subsystem, networking and input provided services to the PC. The PC could run simultaneously with the Mac, and the user could switch between the two in a fashion similar to a KVM switch. The earliest of these systems were 680x0 based systems running System 7. System 7 provided the support for accessing the PC volume from the Mac through its own PC Exchange software, and actual control of the PC hardware was accomplished by way of control panels.

## Miscellaneous

At the time of its release, many users noticed that performance suffered as a result of upgrading from System 6 to System 7, though newer hardware soon made up for the speed difference. Another problem was System 7's large "memory footprint": System 6 could boot the system from a single 800k floppy disk and took up about 600 KB of RAM, whereas System 7 used well over a megabyte. It was some time before the average Mac shipped with enough RAM built in for System 7 to be truly comfortable. System 7 was the first system release that could no longer be usefully run on floppy-only systems. Although most Macintosh models sold at the time included a hard disk as standard equipment, owners of older models were required to upgrade their hardware by buying either a new Mac or an external SCSI hard disk drive if they wished to run System 7.

In order to take advantage of System 7's virtual memory feature, a Macintosh equipped with a paged memory management unit (PMMU) is required. The Motorola 68030 CPU has one built-in, and one can be added to the motherboard of the Motorola 68020-equipped Macintosh II. The other Macintosh model using an 68020, the Macintosh LC, cannot use virtual memory. Apple introduced the 68030-equipped Macintosh LC II shortly after System 7's introduction. Despite the newer processor, the LCII retained the earlier model's 16-bit bus and did not perform any faster than the LC it replaced.

Despite these setbacks, System 7.0 was adopted quite rapidly by Mac users, and quickly became one of the base requirements for new software.

The engineering group within Apple responsible for System 7 came to be known as the "Blue Meanies", named after the blue index cards on which were written the features that could be implemented in a relatively short time as part of Apple's operating system strategy. In comparison, the pink index card features were handled by the Pink group, later becoming the ill-fated [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/) project.

System 7.0 was the last version of the Macintosh operating system that was available for no charge and could be freely redistributed. Although System 7 could be purchased from Apple, the cost was nominal and considered to only cover duplication and media. It was common for Macintosh dealers to allow customers to use the store's demo machines to copy System 7 install disks for the cost of a box of floppies. CD-ROM magazines such as Nautilus included System 7 on their disks. After Mac users downloaded thousands of copies of System 7 from the online services (AOL, CompuServe and GEnie), Apple surveyed the services and based on this popularity started selling the Mac OS as a retail product with System 7.1. Apple continued charging for major operating system upgrades until the release of OS X Mavericks in 2013.

## Version history

Soon after the initial release of System 7, the 7.0.1 minor update was released in October 1991. A patch called "System 7 Tune-Up" also followed, which fixed the "disappearing files" bug in which the system would lose files and added "minimum" and "preferred" memory allotments to an application's Get Info box.

### System 7.1

In August [1992](https://github.com/seanpm2001/WacOS/wiki/1992/), the 7.1 update was released. This was the first version of the system software that Apple charged money for. Of this change, David Pogue wrote:


System 7.1 was remarkable for another reason, too: It was the first system software update Apple didn’t give away. You had to buy it, much to the fury of user groups and online services that had gotten used to making each new system release available to everybody. Backing down in the face of the protests, Apple eventually offered the System 7.1 upgrade kit to user-group and online service members for less than $30. But the writing was on the wall: Apple was jealous of Microsoft, system-software superstore to the world. Many wondered if the upgrade was even worth it. System 7.1 incorporated a huge number of changes, but the vast majority were deep-seated, core-level rewrites that added no usefulness to standard American Mac users.

— David Pogue, MacWorld Macintosh Secrets, 4th edition

New to 7.1 is the Fonts folder. This replaced the often time-consuming method of dragging fonts to and from the System file, introduced in System 7.0; it also replaced the Font/DA Mover application from System 6, which could also be used with 7.0. System 7.1 also included a lot of internal changes to support internationalization of dates, time, and numbers. It was also the first version to support "Enablers", which removed the requirement to release a new version of the system software every time new hardware was released.

A set of specialized versions of 7.1, ranging from 7.1P1 to 7.1P6 (excluding 7.1P4) were created and included with various Performa models that were already available or were released after 7.1. These specialized versions included At Ease, Launcher, and some other changes that were integrated into later versions of the system software.

The first major upgrade was System 7.1.1, also known as "System 7 Pro". This release was a bundle of 7.1 with AppleScript tools, QuickTime and Apple Open Collaboration Environment (AOCE). While System 7 had some trouble running in slightly older machines due to memory footprint, System 7 Pro barely fit into any Macintosh computers at the time. It was most commonly used for its minor bug fixes rather than its new functionality.

Apple joined the AIM alliance (Apple, IBM and Motorola) shortly after the release of System 7 in 1991, and started work on PowerPC-based machines that later became the Power Macintosh family. Support for these machines resulted in System 7.1.2.

System 7.1.2 was never offered for retail sale; it shipped with the first batches of the PowerPC Macs and a 68k version shipped with a small number of Quadra 600 series systems. Later shipments shipped with System 7.5 instead.

System 7.1.2P was the same as 7.1.2, and shipped with the Performa 630, LC 630 and Quadra 630 models that were released between July and November [1994](https://github.com/seanpm2001/WacOS/wiki/1994/).

### System 7.5

The next major release was System 7.5, which included bug fixes from previous updates and added several new features, including:

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Welcome-to-Macintosh/Welcome_to_Macintosh.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Welcome-to-Macintosh/Welcome_to_Macintosh.png)

An updated startup screen featuring a progress bar

A new interactive help system called Apple Guide

A clock in the menu bar (based on the free "SuperClock" control panel by Steve Christensen)

An Apple menu item called Stickies (formerly a third-party application called "PasteIt Notes") which provided virtual Post-It Notes

WindowShade, another former shareware control panel, provided the ability to condense a window down to its title bar. Introduced as a "minimize" feature to compete with Windows 95 as Mac OS had no taskbar or dock.

MacTCP was bundled, enabling any Macintosh to connect to the Internet out of the box for the first time.

The Control Strip (a fast way to change the system volume, control the playback of audio CDs, manage file sharing and printers and change the monitor resolution and color depth) was enabled on desktop Macintosh models for the first time. It had previously only been included with the PowerBook series.

A new Desktop Patterns control panel allowed for tiled patterns up to 128x128 pixels with 8-bit color; previous versions were limited to 8x8 pixel tiles with a maximum of eight possible colors. Similar functionality was found on earlier system versions exclusive to Performa models and was housed in the General Controls panel.

The Extensions Manager (enabling the user to turn extensions and control panels on and off; also based on a formerly third-party control panel)

PowerTalk, a system-level email handling service and the originator of the Keychain system.

The Launcher, a control panel containing shortcut buttons for frequently used programs (in a manner akin to the macOS Dock)

A hierarchical Apple menu (folders within the Apple Menu Items folder would expand into submenus showing their contents. Again, based on a third party control panel; HAM by Microseeds publishing)

System-wide drag & drop for text and other data (selections can be simply dragged with the mouse and dropped to their new destination, bypassing the clipboard)

A scriptable Finder

QuickDraw GX, a 2-D graphics rendering and geometry engine

For the PowerPC only, an advanced, 3d Graphing Calculator, secretly developed at Apple by a former third party contractor

Support for OpenDoc

System 7.5 was codenamed "Capone", a reference to Al Capone and "Chicago", which was the code name for Microsoft's Windows 95, and was also the name of the default system font used in Mac OS until [version 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/).

System 7.5.1 was primarily a bug fix of 7.5, but also introduced a new "Mac OS" startup screen in preparation for Mac clones.

System 7.5.2, released only for the first PCI-based Power Macs, was notable for introducing Apple's new networking architecture, Open Transport.

System 7.5.3, a major bug-fix update that also included Open Transport for other PowerPC-based machines as well as some 68k-based machines. 7.5.3 also made several improvements to the 68k emulator, and added translucent dragging support to the Drag Manager. It also included the first version of Control Strip to be compatible with all Macs. This was also the first version of Mac OS to support SMP. (9500/MP)

System 7.5.3 Revision 2 included: performance enhancements; better reliability for PowerBooks using the third-party RAM Doubler program; improved reliability for PowerBook 500, 2300, and 5300 series computers with the PowerPC Upgrade Card; improved reliability when using the Startup Disk control panel; and improved reliability when copying files to 1 GB hard disks.

System 7.5.3 Revision 2.1 was shipped with the Performa 6400/180 and 6400/200; this particular release was specific to these machines as there were stability problems with System 7.5.3 Release 2 on the new hardware, especially with the video card and transferring files over LocalTalk.

System 7.5.4 was pulled due to a mistake at Apple, in which some components were not included in the installer.

System 7.5.5 included significant performance improvements for virtual memory and memory management on PowerPC-based Macs, including the elimination of one type 11 error. Also included were a number of reliability improvements, such as fixes for Macs using floppy disks equipped with a DOS compatibility card, improved hard disk access for PowerPC PowerBooks and Performa 5400 through 9500 computers, fixes for Macs that included an Apple TV Tuner or Macintosh TV Remote Control, improvements to LocalTalk and networking (especially for the Performa 5400 and 6400), fixes to system startup for the faster 180 MHz Macs (which included PowerPC 604 or 604e processors), improved reliability when using sound-intensive applications on Quadra or Centris computers that contained the PowerPC upgrade card, and improved stability when using multiple background applications and shared printers on a network. System 7.5.5 is also the last System 7 release that can run on 68000-based Macs such as the Macintosh Plus and Macs with ROMs that lack support for 32-bit addressing such as Macintosh IIcx. 7.6 and later required a 68030 processor and 32-bit-addressing-capable ROM and will automatically turn on 32-bit addressing on boot.

### Mac OS 7.6

Mac OS 7.6 (codenamed "Harmony") was the last major update, released in 1997. With 7.6, the operating system was officially called "Mac OS" instead of "System". New features include a revamped Extensions Manager, more native PowerPC code for Power Macs, more bundled Internet tools and utilities, and a more stable Finder with increased memory allocation. In this version, the PowerTalk feature added in 7.5 was removed due to poor application support, and support for a large number of older Macintosh models was dropped.

The minor update to Mac OS 7.6.1 finally ported the 68k exception handling routines to PowerPC, turning type 11 errors into less harmful errors (type 1, 2 or 3, usually) as crashing applications would more often terminate safely instead of crashing the operating system.

Through this period, Apple had been attempting to release a completely new "modern" operating system, named Copland. When the Copland project was abandoned in 1996, Apple announced plans to release an OS update every six months until Rhapsody (which would by 2001 evolve into what was released as Mac OS X) shipped. Two more releases were shipped, now officially branded as "Mac OS" — Mac OS 7.6 and the minor bug fix 7.6.1. Future versions were released as Mac OS 8–8.6 and Mac OS 9–9.2.

### Table of releases

Version number 	Release date 	Computer

7.0 	May 13, 1991 	

7.0.1 	October 21, 1991 	Macintosh Quadra 700/900/950, PowerBook 100/140/170 and some others

7.1 	August 3, 1992 	Macintosh IIvx

PowerBook 180 Macintosh IIvi

7.0.1P 	September 14, 1992 	Macintosh Performa 200/400

7.1P 	October 14, 1992 	Macintosh Performa 600

7.1P2 	April 12, 1993 	Macintosh Performa 405/430/450

7.1P3 	October 18, 1993 	Macintosh Performa 410/460/475/550

7.1.1 (Pro) 	October 21, 1993 	

7.1.1 	PowerBook Duo 250/270c, PowerBook 520/540

7.1P5 	January 1, 1994 	Macintosh Performa 560

7.1P6 	February 1, 1994 	Macintosh Performa 575

7.1.2 	March 14, 1994 	Power Macintosh 6100/7100/8100

7.1.2P 	July 15, 1994 	Quadra 630

7.5 	September 12, 1994 	Macintosh LC 580

7.5.1 	March 23, 1995 	Power Macintosh 6200

7.5.2 	June 19, 1995 	Power Macintosh 9500

7.5.3 	January 1, 1996 	Power Macintosh 5400

7.5.3 Revision 2 	May 1, 1996 	

7.5.3 Revision 2.1 	August 7, 1996 	Performa 6400

7.5.3 Revision 2.2 	Power Macintosh 9500/200, Performa 6360

7.5.5 	September 27, 1996 	Power Macintosh 5500

7.6 	January 7, 1997 	PowerBook 3400c

7.6.1 	April 7, 1997 	PowerBook 2400c Twentieth Anniversary Macintosh

<!-- **This article on classic Mac OS is a stub. You can help by expanding it.** !-->

**This article is a modified copy of the Wikipedia article of the same subject. It needs to be rewritten to be more original.**

***

## Sources

[Wikipedia - System 6](https://en.wikipedia.org/wiki/System_6)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Thursday September 23rd at 3:12 pm`

**Last revised on:** `2021 Thursday September 23rd at 3:12 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021 Thursday September 23rd at 3:12 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
