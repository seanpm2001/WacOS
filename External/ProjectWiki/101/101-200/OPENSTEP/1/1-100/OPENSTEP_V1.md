  
***

# OPENSTEP

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

OpenStep is a defunct object-oriented application programming interface (API) specification for a legacy object-oriented operating system, with the basic goal of offering a [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/)-like environment on non-[NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) operating systems. OpenStep was principally developed by [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) with Sun Microsystems, to allow advanced application development on Sun's operating systems, specifically Solaris. [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) produced a version of OpenStep for its own Mach-based Unix, stylized as OPENSTEP, as well as a version for Windows NT. The software libraries that shipped with OPENSTEP are a superset of the original OpenStep specification, including many features from the original [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/).

## History

In the early era of the Sun Microsystems history, Sun machines had been programmed at a relatively low-level making calls directly to the underlying Unix operating system and producing a graphical user interface (GUI) using the X11 system. This led to complex programming even for simple projects. An attempt to address this with an object oriented programming model was made in the mid-1980s with Sun's NeWS windowing system, but the combination of a complex application programming interface (API) and generally poor performance led to little real-world use and the system was eventually abandoned.

Sun then began looking for other options. [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/) was considered to be a competitor in the operating system and object markets, and Microsoft's Cairo was at least a consideration, even without any product releases from either. [Taligent's](https://github.com/seanpm2001/WacOS/wiki/Taligent/) theoretical newness was often compared to [NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) older but mature and commercially established platform. Sun held exploratory meetings with [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/) before deciding upon building out its object application framework OpenStep in partnership with [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) /as a "preemptive move against [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/) and Cairo". Bud Tribble, a founding designer of the Macintosh and of [NeXTStep](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/), was now SunSoft's Vice President of Object Products to lead this decision. The [1993](https://github.com/seanpm2001/WacOS/wiki/1993/) partnership included a $10 million investment from Sun into [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/). The deal was described as "the first unadulterated piece of good news in the [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) community in the last four years".

The basic concept was to take a cut-down version of the [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) operating system's object layers and adapt them to run on Sun's Solaris operating system, more specifically, Solaris on SPARC-based hardware. Most of the OpenStep effort was to strip away those portions of NeXTSTEP that depended on Mach or NeXT-specific hardware being present. This resulted in a smaller system that consisted primarily of Display PostScript, the [Objective-C](https://github.com/seanpm2001/WacOS/wiki/Objective-C/) runtime and compilers, and the majority of the [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) [Objective-C](https://github.com/seanpm2001/WacOS/wiki/Objective-C/) libraries. Not included was the basic operating system, or the lower-level display system.

[Steve Jobs](https://github.com/seanpm2001/WacOS/wiki/Steve_Jobs/) said "We are ahead today, but the race is far from over. ... [In 1996,] Cairo will be very close behind, and [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/) will be very far behind." Sun's CEO Scott McNealy said, "We have no insurance policy. We have made a firm one-company, one-architecture decision, not like [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/) getting a trophy spouse by signing up HP."

The first draft of the API was published by [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) in mid [1994](https://github.com/seanpm2001/WacOS/wiki/1994/). Later that year they released an OpenStep compliant version of [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) as OPENSTEP, supported on several of their platforms as well as Sun SPARC systems. NeXT submitted the OpenStep specification to the industry's object standards bodies. The official OpenStep API, published in September [1994](https://github.com/seanpm2001/WacOS/wiki/1994/), was the first to split the API between Foundation and Application Kit and the first to use the "NS" prefix.] Early versions of [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) use an "NX" prefix and contain only the Application Kit, relying on standard Unix [libc](https://github.com/seanpm2001/WacOS/wiki/C-(Programming-language)/) types for low-level data structures. OPENSTEP remaine [NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) primary operating system product until the company was purchased by Apple Computer in [1997](https://github.com/seanpm2001/WacOS/wiki/1997/). OPENSTEP was then combined with technologies from the existing [classic Mac OS](https://github.com/seanpm2001/WacOS/wiki/Classic-Mac-OS/) to produce [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/). iPhone and iPad's [iOS](https://github.com/seanpm2001/WacOS/wiki/iOS/) is also a descendant of OPENSTEP, but targeted at touch devices.

Sun originally adopted the OpenStep environment with the intent of complementing Sun's CORBA-compliant object system, Solaris NEO (formerly known as Project DOE), by providing an object-oriented user interface toolkit to complement the object-oriented CORBA plumbing. The port involved integrating the OpenStep AppKit with the Display PostScript layer of the Sun X11 server, making the AppKit tolerant of multi-threaded code (as Project DOE was inherently heavily multi-threaded), implementing a Solaris daemon to simulate the behavior of Mach ports, extending the SunPro [C++](https://github.com/seanpm2001/WacOS/wiki/C-plus-plus/) compiler to support Objective-C using [NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) [ObjC](https://github.com/seanpm2001/WacOS/wiki/Objective-C/) runtime, writing an X11 window manager to implement the [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) look and feel as much as possible, and integrating the [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) development tools, such as Project Builder and Interface Builder, with the SunPro compiler. In order to provide a complete end-user environment, Sun also ported the [NeXTSTEP-3.3](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP-version-history/) versions of several end-user applications, including Mail.app, Preview.app, Edit.app, Workspace Manager, and the Dock.

The OpenStep and CORBA parts of the products were later split, and NEO was released in late [1995](https://github.com/seanpm2001/WacOS/wiki/1995/) without the OpenStep environment. In March [1996](https://github.com/seanpm2001/WacOS/wiki/1996/), Sun announced Joe, a product to integrate NEO with Java. Sun shipped a beta release of the OpenStep environment for Solaris on July 22, [1996](https://github.com/seanpm2001/WacOS/wiki/1996/), and made it freely available for download in August [1996](https://github.com/seanpm2001/WacOS/wiki/1996/) for non-commercial use, and for sale in September [1996](https://github.com/seanpm2001/WacOS/wiki/1996/). OpenStep/Solaris was shipped only for the SPARC architecture.

## Description

OpenStep differs from NeXTSTEP in various ways:

[NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) is an operating system, whereas OpenStep is an API.

Unlike [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/), OpenStep does not require the Mach kernel.

Each version of [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) has a specific endianness: big endian for Motorola 68K processors, and little endian for x86 processors, for example. OpenStep is "endian-free".

OpenStep introduces new classes and memory management capabilities.

The OpenStep API specification defines three major components: Foundation Kit, the software framework; Application Kit, the GUI and graphics front-end; and Display PostScript, a 2D graphics system (for drawing windows and other graphics on the screen).

## Building on OpenStep

The standardization on OpenStep also allowed for the creation of several new library packages that were delivered on the OPENSTEP platform. Unlike the operating system as a whole, these packages were designed to run stand-alone on practically any operating system. The idea was to use OpenStep code as a basis for network-wide applications running across different platforms, as opposed to using CORBA or some other system.

Primary among these packages was Portable Distributed Objects (PDO). PDO was essentially an even more "stripped down" version of OpenStep containing only the Foundation Kit technologies, combined with new libraries to provide remote invocation with very little code. Unlike OpenStep, which defined an operating system that applications would run in, under PDO the libraries were compiled into the application itself, creating a stand-alone "native" application for a particular platform. PDO was small enough to be easily portable, and versions were released for all major server vendors.

In the mid-1990s, [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) staff took to writing in solutions to various CORBA magazine articles in a few lines of code, whereas the original article would fill several pages. Even though using PDO required the installation of a considerable amount of supporting code ([Objective-C](https://github.com/seanpm2001/WacOS/wiki/Objective-C/) and the libraries), PDO applications were nevertheless considerably smaller than similar CORBA solutions, typically about one-half to one-third the size.

The similar D'OLE provided the same types of services, but presented the resulting objects as COM objects, with the goal of allowing programmers to create COM services running on high-powered platforms, called from Microsoft Windows applications. For instance one could develop a high-powered financial modeling application using D'OLE, and then call it directly from within Microsoft Excel. When D'OLE was first released, OLE by itself only communicated between applications running on a single machine. PDO enabled [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) to demonstrate Excel talking to other Microsoft applications across a network before Microsoft themselves were able to implement this functionality (DCOM).

Another package developed on OpenStep was Enterprise Objects Framework (EOF), a tremendously powerful (for the time) object-relational mapping product. EOF became very popular in the enterprise market, notably in the financial sector where OPENSTEP caused something of a minor revolution.

## Implementations

### OPENSTEP for Mach

[NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) first operating system was [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/), a sophisticated Mach-UNIX based operating system that originally ran only on [NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) Motorola 68k-based workstations and that was then ported to run on 32-bit Intel x86-based "IBM-compatible" personal computers, PA-RISC-based workstations from Hewlett-Packard, and SPARC-based workstations from Sun Microsystems.

NeXT completed an implementation of OpenStep on their existing Mach-based OS and called it OPENSTEP for Mach 4.0 (July, 1996), 4.1 (December, 1996), and 4.2 (January, 1997). It was, for all intents, [NeXTSTEP 4.0](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP-version-history/), and still retained flagship [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) technologies (such as DPS, UNIX underpinnings, user interface characteristics like the Dock and Shelf, and so on), and retained the classic [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) user interface and styles. OPENSTEP for Mach was further improved, in comparison to NeXTSTEP 3.3, with vastly improved driver support – however the environment to actually write drivers was changed with the introduction of the object-oriented DriverKit.

OPENSTEP for Mach supported Intel x86-based PC's, Sun's SPARC workstations, and [NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) own 68k-based architectures, while the HP PA-RISC version was dropped. These versions continued to run on the underlying Mach-based OS used in [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/). OPENSTEP for Mach became [NeXT's](https://github.com/seanpm2001/WacOS/wiki/NeXT/) primary OS from [1995](https://github.com/seanpm2001/WacOS/wiki/1995/) on, and was used mainly on the Intel platform. In addition to being a complete OpenStep implementation, the system was delivered with a complete set of [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) libraries for backward compatibility. This was an easy thing to do in OpenStep due to library versioning, and OPENSTEP did not suffer in bloat because of it.

### Solaris OpenStep

In addition to the OPENSTEP for Mach port for SPARC, Sun and [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) developed an OpenStep compliant set of frameworks to run on Sun's Solaris operating system. After developing Solaris OpenStep, Sun lost interest in OpenStep and shifted its attention toward Java. As a virtual machine development environment, Java served as a direct competitor to OpenStep.

### OPENSTEP Enterprise

[NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) also delivered an implementation running on top of Windows NT 4.0 called OPENSTEP Enterprise (often abbreviated OSE). This was an unintentional demonstration on the true nature of the portability of programs created under the OpenStep specification. Programs for OPENSTEP for Mach could be ported to OSE with little difficulty. This allowed their existing customer base to continue using their tools and applications, but running them on Windows, to which many of them were in the process of switching. Never a clean match from the UI perspective, probably due to OPENSTEP's routing of window graphics through the Display Postscript server—which was also ported to Windows—OSE nevertheless managed to work fairly well and extended OpenStep's commercial lifespan.

OPENSTEP and OSE had two revisions (and one major one that was never released) before [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) was purchased by Apple in [1997](https://github.com/seanpm2001/WacOS/wiki/1997/).

### Rhapsody, Mac OS X Server 1.0

After acquiring [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/), Apple intended to ship [Rhapsody](https://github.com/seanpm2001/WacOS/wiki/Rhapsody/) as a reworked version of OPENSTEP for Mach for both the Mac and standard PCs. [Rhapsody](https://github.com/seanpm2001/WacOS/wiki/Rhapsody/) was OPENSTEP for Mach with a [Copland](https://github.com/seanpm2001/WacOS/wiki/Copland/) appearance from [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) and support for Java and Apple's own technologies, including ColorSync and [QuickTime](https://github.com/seanpm2001/WacOS/wiki/QuickTime/); it could be regarded as OPENSTEP 5. Two developer versions of [Rhapsody](https://github.com/seanpm2001/WacOS/wiki/Rhapsody/) were released, known as Developer Preview 1 and 2; these ran on a limited subset of both Intel and [PowerPC](https://github.com/seanpm2001/WacOS/wiki/PowerPC/) hardware. [Mac OS X Server 1.0](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-Server-1-0/) was the first commercial release of this operating system, and was delivered exclusively for [PowerPC](https://github.com/seanpm2001/WacOS/wiki/PowerPC/) Mac hardware.

### Darwin, Mac OS X 10.0 and later

After replacing the Display Postscript WindowServer with Quartz, and responding to developers by including better backward compatibility for [classic Mac OS](https://github.com/seanpm2001/WacOS/wiki/Classic-Mac-OS/) applications through the addition of Carbon, Apple released [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/) and [Mac OS X Server](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-Server-1-0/), starting at version 10.0; [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-version-history/) is now named [macOS](https://github.com/seanpm2001/WacOS/wiki/MacOS/).

macOS's primary programming environment is essentially OpenStep (with certain additions such as XML property lists and URL classes for Internet connections) with [macOS](https://github.com/seanpm2001/WacOS/wiki/MacOS/) ports of the development libraries and tools, now called Cocoa.

[macOS](https://github.com/seanpm2001/WacOS/wiki/MacOS/) has since become the single most popular desktop Unix-like operating system in the world, although [macOS](https://github.com/seanpm2001/WacOS/wiki/MacOS/) is no longer an OpenStep compliant operating system.

### GNUstep

GNUstep, a free software implementation of the [NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) libraries, began at the time of [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/), predating OPENSTEP. While OPENSTEP and OSE were purchased by Apple, who effectively ended the commercial development of implementing OpenStep for other platforms, GNUstep is an ongoing open source project aiming to create a portable, free software implementation of the Cocoa/OPENSTEP libraries.

GNUstep also features a fully functional development environment, reimplementations of some of the newer innovations from [macOS's](https://github.com/seanpm2001/WacOS/wiki/MacOS/) Cocoa framework, as well as its own extensions to the API. 

**This article is a modified copy of its Wikipedia counterpart and needs to be rewritten for originality.**

***

## See also

[NeXT](https://github.com/seanpm2001/WacOS/wiki/NeXT/) - The main company hehind NeXTSTEP

[NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) - The main operating system from NeXT.

[NeXT Character set](https://github.com/seanpm2001/WacOS/wiki/NeXT-character-set/) - The character encoding set used by NeXTSTEP.

[Objective-C](https://github.com/seanpm2001/WacOS/wiki/Objective-C/) - The language used in NeXTSTEP

[Mac OS X 10.0 (Cheetah)](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/) - The successor to OPENSTEP

***

## Sources

[Wikipedia - OpenStep](https://en.wikipedia.org/wiki/OpenStep)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Saturday September 25th at 3:25 pm`

**Last revised on:** `2021 Saturday September 25th at 3:25 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021 Saturday September 25th at 3:25 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
