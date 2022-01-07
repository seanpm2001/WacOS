  
***

# Copland (Operating system)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Copland/Copland_open_file_dialog_screenshot.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Copland/Copland_open_file_dialog_screenshot.png)

<!--
<details>
<summary><p>Click/tap here to expand/collapse</p>
<p>the dropdown containing the Mac OS X 10.2 logo</p></summary>

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/MacOS_X/10.2_Jaguar/Jaguar-logo.png)

</details>
!-->

( **Predecessor:** [Mac OS 7](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-7/) | **Successor:** [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) )

Copland is an operating system developed by Apple for Macintosh computers between [1994](https://github.com/seanpm2001/WacOS/wiki/1994/) and [1996](https://github.com/seanpm2001/WacOS/wiki/1996/) but never commercially released. It was intended to be released as [System 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/), and later, [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/). Planned as a modern successor to the aging System 7, Copland introduced protected memory, preemptive multitasking, and several new underlying operating system features, while retaining compatibility with existing Mac applications. Copland's tentatively planned successor, codenamed Gershwin, was intended to add more advanced features such as application-level multithreading.

Development officially began in March [1994](https://github.com/seanpm2001/WacOS/wiki/1994/). Over the next several years, previews of Copland garnered much press, introducing the Mac audience to basic concepts of modern operating system design such as object orientation, crash-proofing, and multitasking. In May 1996, Gil Amelio stated that Copland was the primary focus of the company, aiming for a late-year release. Internally, however, the development effort was beset with problems due to dysfunctional corporate personnel and project management. Development milestones and developer release dates were missed repeatedly.

Ellen Hancock was hired to get the project back on track, but quickly concluded it would never ship. In August [1996](https://github.com/seanpm2001/WacOS/wiki/1996/), it was announced that Copland was canceled and Apple would look outside the company for a new operating system. Among many choices, they selected OpenStep and purchased NeXT in 1997 to obtain it. In the interim period, while [OpenStep](https://github.com/seanpm2001/WacOS/wiki/OPENSTEP/) was ported to the Mac, Apple released a much more legacy-oriented [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) in [1997](https://github.com/seanpm2001/WacOS/wiki/1997/), followed by [Mac OS 9](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-9/) in [1999](https://github.com/seanpm2001/WacOS/wiki/1999/). [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/) became Apple's next-generation operating system with its release in [2001](https://github.com/seanpm2001/WacOS/wiki/2001/). All of these releases bear functional or cosmetic influence from Copland.

The Copland development effort is associated with empire-building, feature creep, and project death march. In [2008](https://github.com/seanpm2001/WacOS/wiki/2008/), PC World named Copland on a list of the biggest project failures in information technology (IT) history.

## Design

### Mac OS legacy

The prehistory of Copland begins with an understanding of the Mac OS legacy, and its architectural problems to be solved.

Launched in [1984](https://github.com/seanpm2001/WacOS/wiki/1984/), the Macintosh and its operating system were designed from the start as a single-user, single-tasking system, which allowed the hardware development to be greatly simplified. As a side effect of this single application model, the original Mac developers were able to take advantage of several compromising simplifications that allowed great improvements in performance, running even faster than the much more expensive [Lisa](https://github.com/seanpm2001/WacOS/wiki/Apple-Lisa/). But this design also led to several problems for future expansion.

By assuming only one program would be running at a time, the engineers were able to ignore the concept of reentrancy, which is the ability for a program (or code library) to be stopped at any point, asked to do something else, and then return to the original task. In the case of QuickDraw for example, this means the system can store state information internally, like the current location of the window or the line style, knowing it would only change under control of the running program. Taking this one step further, the engineers left most of this state inside the application rather than in QuickDraw, thus eliminating the need to copy this data between the application and library. QuickDraw found this data by looking at known locations within the applications.

This concept of sharing memory is a significant source of problems and crashes. If an application program writes incorrect data into these shared locations, it could cause QuickDraw to crash, thereby causing the computer to crash. Likewise, any problem in QuickDraw could cause it to overwrite data in the application, once again leading to crashes. In the case of a single-application operating system this was not a fatal limitation, because in that case a problem in either would require the application, or computer, to be restarted anyway.

The other main issue was that early Macs lack a memory management unit (MMU), which precludes the possibility of several fundamental modern features. An MMU provides memory protection to ensure that programs cannot accidentally overwrite other program's memory, and provisions shared memory that allows data to be easily passed among libraries. Lacking shared memory, the API was instead written so the operating system and application shares all memory, which is what allows QuickDraw to examine the application's memory for settings like the line drawing mode or color.

The Macintosh lacks multitasking but tries to fake it, and it insists on a complicated user interface but leaves much of the work up to the application. These are serious drawbacks, and it is difficult to imagine elegant repairs for them.

— Adam Brooks Webber, Byte (September 1986)

These limits meant that supporting the multitasking of more than one program at a time would be difficult, without rewriting all of this operating system and application code. Yet doing so would mean the system would run unacceptably slow on existing hardware. Instead, Apple adopted a system known as MultiFinder in 1987, which keeps the running application in control of the computer, as before, but allows an application to be rapidly switched to another, normally simply by clicking on its window. Programs that are not in the foreground are periodically given short bits of time to run, but as before, the entire process is controlled by the applications, not the operating system.

Because the operating system and applications all share one memory space, it is possible for a bug in any one of them to corrupt the entire operating system, and crash the machine. Under MultiFinder, any crash anywhere will crash all running programs. Running multiple applications potentially increases the chances of a crash, making the system potentially more fragile.

Adding greatly to the severity of the problem is the patching mechanism used to add functions to the operating system, known as CDEVs and INITs or Control Panels and Extensions. Third party developers also make use of this mechanism to add features, including screensavers and a hierarchical Apple menu. Some of these third-party control panels became almost universal, like the popular After Dark screensaver package. Because there was no standard for use of these patches, it is not uncommon for several of these add-ons — including Apple's own additions to the OS — to use the same patches, and interfere with each other, leading to more crashing.

### Copland design

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Copland/Copland_organization.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Copland/Copland_organization.svg)

Copland was designed to consist of the Mac OS on top of a microkernel named Nukernel, which would handle basic tasks such as application startup and memory management, leaving all other tasks to a series of semi-special programs known as servers. For instance, networking and file services would not be provided by the kernel itself, but by servers that would be sent requests through interapplication communications. Copland consists of the combination of Nukernel, various servers, and a suite of application support libraries to provide implementations of the well-known classic Macintosh programming interface.

Application services are offered through a single program known officially as the Cooperative Macintosh Toolbox environment, but universally referred to as the Blue Box. The Blue Box encapsulates an existing System 7 operating system inside a single process and address space. Mac programs run inside the Blue Box much as they do under System 7, as cooperative tasks that use the non-reentrant Toolbox calls. A worst-case scenario is that an application in the Blue Box crashes, taking down the entire Blue Box instance with it. This does not result in the system as a whole going down, however, and the Blue Box can be restarted.
Copland runtime architecture. The purple boxes show threads of control, while the heavy lines show different memory partitions. In the upper left is the Blue Box, running several System 7 applications (blue) and the toolbox code supporting them (green). Two headless applications are also running in their own spaces, providing file and web services. At the bottom are the OS servers, running in the same memory space as the kernel, indicating colocation.

New applications written with Copland in mind, are able to directly communicate with the system servers and thereby gain many advantages in terms of performance and scalability. They can also communicate with the kernel to launch separate applications or threads, which run as separate processes in protected memory, as in most modern operating systems. These separate applications cannot use non-reentrant calls like QuickDraw, however, and thus could have no user interface. Apple suggested that larger programs could place their user interface in a normal Macintosh application, which would then start worker threads externally.

Another key feature of Copland is that it is fully PowerPC (PPC) native. System 7 had been ported to the PowerPC with great success; large parts of the system run as PPC code, including both high-level functions, such as most of the user interface toolbox managers, and low-level functions, such as interrupt management. There is enough 68k code left in the system to be run in emulation, and especially user applications, however that the operating system must map some data between the two environments. In particular, every call into the Mac OS requires a mapping between the interrupt systems of the 68k and PPC. Removing these mappings would greatly improve general system performance. At WWDC 1996, engineers claimed that system calls would execute as much as 50% faster.

Copland is also based on the then-recently defined Common Hardware Reference Platform, or CHRP, which standardized the Mac hardware to the point where it could be built by different companies and can run other operating systems (Solaris and AIX were two of many mentioned). This was a common theme at the time; many companies were forming groups to define standardized platforms to offer an alternative to the "Wintel" platform that was rapidly becoming dominant — examples include 88open, Advanced Computing Environment, and the AIM alliance.

The fundamental second-system effect to challenge Copland's development and adoption would be getting all of these functions to fit into an ordinary Mac. System 7.5 already uses up about 2.5 megabytes (MB) of RAM, which is a significant portion of the total RAM in most contemporaneous machines. Copland is two systems in one, as its native foundation also hosts Blue Box, containing essentially a complete copy of System 7.5. Copland thus uses a Mach-inspired memory management system and relies extensively on shared libraries, with the goal being for Copland to be only some 50% larger than 7.5.

## History

### Pink and Blue

In March [1988](https://github.com/seanpm2001/WacOS/wiki/1988/), technical middle managers at Apple held an offsite meeting to plan the future course of Mac OS development. Ideas were written on index cards; features that seemed simple enough to implement in the short term (like adding color to the user interface) were written on blue cards; longer-term goals—such as preemptive multitasking—were on pink cards; and long-range ideas like an object-oriented file system were on red cards. Development of the ideas contained on the blue and pink cards was to proceed in parallel, and at first, the two projects were known simply as "blue" and "pink". Apple intended to have the "blue" team (who came to call themselves the "Blue Meanies" after characters in the film Yellow Submarine) release an updated version of the existing Macintosh operating system in the 1990–1991 timeframe, and the Pink team to release an all-new OS around [1993](https://github.com/seanpm2001/WacOS/wiki/1993/).

The Blue team delivered what became known as [System 7](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-7/) on May 13, 1991, but the Pink team suffered from second-system effect and its release date continued to slip into the indefinite future. Some of the reason for this can be traced to problems that would become widespread at Apple as time went on; as Pink became delayed, its engineers moved to Blue instead. This left the Pink team constantly struggling for staffing, and suffering from the problems associated with high employee turnover. Management ignored these sorts of technical development issues, leading to continual problems delivering working products.

At this same time, the recently released [NeXTSTEP](https://github.com/seanpm2001/WacOS/wiki/NeXTSTEP/) was generating intense interest in the developer world. Features that were originally part of Red, were folded into Pink, and the Red project (also known as "Raptor") was eventually canceled. This problem was also common at Apple during this period; in order to chase the "next big thing", middle managers would add new features to their projects with little oversight, leading to enormous problems with feature creep. In the case of Pink, development eventually slowed to the point the project appeared moribund.

### Taligent

**Main article:** [Taligent](https://github.com/seanpm2001/WacOS/wiki/Taligent/)

On April 12, 1991, Apple CEO John Sculley performed a secret demonstration of Pink running on an IBM PS/2 Model 70 to a delegation from IBM. Though the system was not fully functional, it resembled System 7 running on a PC. IBM was extremely interested, and over the next few months, the two companies formed an alliance to further development of the system. These efforts became public in early 1992, under the new name "Taligent". At the time, Sculley summed up his concerns with Apple's own ability to ship Pink when he stated "We want to be a major player in the computer industry, not a niche player. The only way to do that is to work with another major player.

Infighting at the new joint company was legendary, and the problems with Pink within Apple soon appeared to be minor in comparison. Apple employees made T-shirts graphically displaying their prediction that the result would be an IBM-only project. On December 19, [1995](https://github.com/seanpm2001/WacOS/wiki/1995/), Apple officially pulled out of the project. IBM continued working alone with Taligent, and eventually released its application development portions under the new name "CommonPoint". This saw little interest and the project disappeared from IBM's catalogs within months.
Business as usual

While Taligent efforts continued, very little work addressing the structure of the original OS was carried out. Several new projects started during this time, notably the Star Trek project, a port of System 7 and its basic applications to Intel-compatible x86 machines, which reached internal demo status. But as Taligent was still a concern, it was difficult for new OS projects to gain any traction.

Instead, Apple's Blue team continued adding new features to the same basic OS. During the early 1990s Apple released a series of major new packages to the system; among them are QuickDraw GX, Open Transport, OpenDoc, PowerTalk, and many others. Most of these were larger than the original operating system. Problems with stability that had existed even with small patches, grew along with the size and requirements of these packages, and by the mid-1990s the Mac had a reputation for instability and constant crashing.

As the stability of the operating system collapsed, the ready answer was that Taligent would fix this with all its modern foundation of full reentrance, preemptive multitasking, and protected memory. When the Taligent efforts collapsed, Apple remained with an aging OS and no designated solutions. By 1994 the press buzz surrounding the upcoming release of Windows 95 started to crescendo, often questioning Apple's ability to respond to the challenge it presented. The press turned on the company, often introducing Apple's new projects as failures in the making.

### Another try

Given this pressure, the collapse of Taligent, the growing problems with the existing operating system, and the release of System 7.5 in late 1994, Apple management decided that the decade-old operating system had run its course. A new system that did not have these problems was needed, and soon. Since so much of the existing system would be difficult to rewrite, Apple developed a two-stage approach to the problem.

In the first stage, the existing system would be moved on top of a new kernel-based OS with built-in support for multitasking and protected memory. The existing libraries, like QuickDraw, would take too long to be rewritten for the new system and would not be converted to be reentrant. Instead, a single paravirtualized machine, the Blue Box, keeps applications and legacy code such as QuickDraw in a single memory block so they continue to run as they had in the past. Blue Box runs in a distinct Copland memory space, so crashing legacy applications or extensions within Blue Box cannot crash the entire machine.

In the next stage of the plan, once the new kernel was in place and this basic upgrade was released, development would move on to rewriting the older libraries into new forms that could run directly on the new kernel. At that point, applications would gain some added modern features.

In the musical code-naming pattern where System 7.5 is code-named "Mozart", this intended successor is named "Copland" after composer Aaron Copland. In turn, its proposed successor system, Gershwin, would complete the process of moving the entire system to the modern platform, but work on Gershwin would never officially begin.

### Development

The Copland project was first announced in March 1995. Parts of Copland, most notably an early version of the new file system, were demonstrated at Apple's Worldwide Developers Conference in May 1995. Apple also promised that a beta release of Copland would be ready by the end of the year, for final commercial release in early 1996. Gershwin would follow the next year. Throughout the year, Apple released several mock-ups to various magazines showing what the new system would look like, and commented continually that the company was fully committed to this project. By the end of the year, however, no Developer Release had been produced.
Copland's open file dialog box, with a preview area on the right. The stacked folders area on the left is intended to provide a visual path to the current selection, but this was later abandoned as being too complex. The user is currently using a favorite location shortcut.

As had happened in the past during the development of Pink, developers within Apple soon started abandoning their own projects in order to work on the new system. Middle management and project leaders fought back by claiming that their project was vital to the success of the system, and moving it into the Copland development stream. Thus, it could not be canceled along with their employees being removed to work on some other part of Copland anyway. This process took on momentum across the next year.

"Anytime they saw something sexy it had to go into the OS." said Jeffrey Tarter, publisher of the software industry newsletter Softletter. "There were little groups all over Apple doing fun things that had no earthly application to Apple's product line." What resulted was a vicious cycle: As the addition of features pushed back deadlines, Apple was compelled to promise still more functions to justify the costly delays. Moreover, this Sisyphean pattern persisted at a time when the company could scarcely afford to miss a step.

Soon the project looked less like a new operating system and more like a huge collection of new technologies; QuickDraw GX, System Object Model (SOM), and OpenDoc became core components of the system, while completely unrelated technologies like a new file management dialog box (the open dialog) and themes support appeared also. The feature list grew much faster than the features could be completed, a classic case of creeping featuritis. An industry executive noted that "The game is to cut it down to the three or four most compelling features as opposed to having hundreds of nice-to-haves, I'm not sure that's happening."

As the "package" grew, testing it became increasingly difficult and engineers were commenting as early as 1995 that Apple's announced 1996 release date was hopelessly optimistic: "There's no way in hell Copland ships next year. I just hope it ships in 1997."

In mid-1996, information was leaked that Copland would have the ability to run applications written for other operating systems including Windows NT. Simultaneously allegedly confirmed by Copland engineers and authoritatively denied by Copland project management, this feature had supposedly been in development for more than 3 years. One user claimed to have been told about these plans by members of the Copland development team. Some analysts projected that this ability would increase Apple's penetration into the enterprise market, others said it was "game over" and was only a sign of the Mac platform's irrelevancy.

### Developer Release

At WWDC [1996](https://github.com/seanpm2001/WacOS/wiki/1996/), Apple's new CEO, Gil Amelio, used the keynote to talk almost exclusively about Copland, now known as [System 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/). He repeatedly stated that it was the only focus of Apple engineering and that it would ship to developers in a few months with a full release planned for late 1996. Very few, if any, demos of the running system were shown at the conference. Instead, various pieces of the technology and user interface that would go into the package (such as a new file management dialog) were demonstrated. Little of the core system's technology was demonstrated and the new file system that had been shown a year earlier was absent.

There was one way to actually use the new operating system, by signing up for time in the developer labs. This did not go well:

There was a hands-on demo of the current state of [OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/). There were tantalizing glimpses of the goodies to come, but the overall experience was awful. It does not yet support text editing, so you couldn’t actually do anything except open and view documents (any dialog field that needed something typed into it was blank and dead). Also, it was incredibly fragile and crashed repeatedly, often corrupting system files on the disk in the process. The demo staff reformatted and rebuilt the hard disks at regular intervals. It was incredible that they even let us see the beast.

Several people at the show complained about the microkernel's lack of sophistication, notably the lack of symmetric multiprocessing, a feature that would be exceedingly difficult to add to a system due to ship in a few months. After that, Amelio came back on stage and announced that they would be adding that to the feature list.

In August [1996](https://github.com/seanpm2001/WacOS/wiki/1996/), "Developer Release 0" was sent to a small number of selected partners. Far from demonstrating improved stability, it often crashed after doing nothing at all, and was completely unusable for development. In October, Apple moved the target delivery date to "sometime", hinting that it might be [1997](https://github.com/seanpm2001/WacOS/wiki/1997/). One of the groups most surprised by the announcement was Apple's own hardware team, who had been waiting for Copland to allow the [PowerPC](https://github.com/seanpm2001/WacOS/wiki/PowerPC/) be natively represented, unburdened of software legacy. Members of Apple's software QA team joked that, given current resources and the number of bugs in the system, they could clear the program for shipping sometime around 2030.

### Cancellation

Later in August [1996](https://github.com/seanpm2001/WacOS/wiki/1996/), the situation was no better. Amelio complained that Copland was "just a collection of separate pieces, each being worked on by a different team ... that were expected to magically come together somehow." Hoping to salvage the situation, Amelio hired Ellen Hancock away from National Semiconductor to take over engineering and get Copland development back on track.

After a few months on the job, Hancock came to the conclusion that the situation was hopeless; given current development and engineering, she believed Copland would never ship. Instead, she suggested that the various user-facing technologies in Copland be rolled out in a series of staged releases, instead of a single big release.

To address the aging infrastructure below these technologies, Amelio suggested looking outside the company for an unrelated new operating system. Candidates considered were Sun's Solaris and Windows NT. Hancock reportedly was in favor of going with Solaris, while Amelio preferred Windows. Amelio even reportedly called Bill Gates to discuss the idea, and Gates promised to put Microsoft engineers to work porting QuickDraw to NT.

Apple officially canceled Copland in August 1996 and reused the [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/) product name for codename Tempo, a Copland-inspired major update to Mac OS 7.6. The CD envelopes for the developer's release had been printed, but the discs had not been mastered.

After lengthy discussions with Be and rumors of a merger with Sun Microsystems, many were surprised at Apple's December 1996 announcement that they were purchasing NeXT and bringing Steve Jobs on in an advisory role. Amelio quipped that they "choose Plan A instead of Plan Be." The project to port OpenStep to the Macintosh platform was named Rhapsody and was to be the core of Apple's cross-platform operating system strategy. This would inherit OpenStep's existing support for PowerPC, Intel x86, and DEC Alpha CPU architectures, and an implementation of the OPENSTEP libraries running on Windows NT. This would in effect open the Windows application market to Macintosh developers as they could license the library from Apple for distribution with their product, or depend on an existing installation.

## Legacy

Following Hancock's plan, development of System 7.5 continued, with several technologies originally slated for Copland being incorporated into the base OS. Apple embarked on a buying campaign, acquiring the rights to various third-party system enhancements and integrating them into the OS. The Extensions Manager, hierarchical Apple menu, collapsing windows, the menu bar clock, and sticky notes—all were developed outside of Apple. Stability and performance were improved by Mac OS 7.6, which dropped the "System" moniker in favor of "Mac OS". Eventually, many features developed for Copland, including the new multithreaded Finder and support for themes (the default Platinum was the only theme included) were rolled into the unreleased beta of Mac OS 7.7, which was instead rebranded and launched as [Mac OS 8](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-8/).

With the return of Jobs, this rebranding to version 8 also allowed Apple to exploit a legal loophole to terminate third-party manufacturers' licenses to [System 7](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-7/) and effectively shut down the Macintosh clone market. Later, Mac OS 8.1 finally added the new file system and Mac OS 8.6 updated the nanokernel to handle limited support for preemptive tasks. Its interface is Multiprocessing Services 2.x and later, but there is no process separation and the system still uses cooperative multitasking between processes. Even a process that is Multiprocessing Services-aware still has a part that runs in the Blue Box, a task that also runs all single-threaded programs and the only task that can run 68k code.

The Rhapsody project was canceled after several Developer Preview releases, support for running on non-Macintosh platforms was dropped, and it was eventually released as [Mac OS X Server 1.0](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-Server-1-0/). In [2001](https://github.com/seanpm2001/WacOS/wiki/2001/) this foundation was coupled to the Carbon library and Aqua user interface to form the modern Mac OS X product. Versions of Mac OS X prior to the Intel release of [Mac OS X 10.4 (Tiger](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-4-Tiger/)), also use the rootless Blue Box concept in the form of Classic to run applications written for older versions of Mac OS. Several features originally seen in Copland demos, including its advanced Find command, built-in Internet browser, piles of folders, and support for video-conferencing, have reappeared in subsequent releases of [Mac OS X](https://github.com/seanpm2001/WacOS/wiki/Mac-OS-X-10-0-Cheetah/) as Spotlight, Safari, Stacks, and iChat AV, respectively, although the implementation and user interface for each feature is very different.

## Hardware requirements

According to the documentation included in the Developer Release, Copland supports the following hardware configurations:

NuBus-based Macintoshes: 6100/60, 6100/60AV (no AV functions), 6100/66, 6100/66 AV (no AV functions), 6100/66 DOS (no DOS functions), 7100/66, 7100/66 AV (no AV functions), 7100/80, 7100/80 AV (no AV functions), 8100/80/ 8100/100/ 8100/100 AV (no AV functions), 8100/110

NuBus-based Performas: 6110CD, 6112CD, 6115CD, 6117CD, 6118CD

PCI-based Macintoshes: 7200/70, 7200/90, 7500/100, 8500/120, 9500/120, 9500/132

Drives formatted with Drive Setup (other initialization software may work; if the user has trouble, he or she can try reinitializing with Drive Setup 1.0.4 or later).

For builds up to and including DR1, the installer is set to ensure the user has System 7.5 or later on a hard disk 250MB or greater.

Monitors connected to either built-in video or a card set to 256 colors (8-bit) or Thousands (16-bit).

***

## Sources

[Wikipedia - Copland (operating system)](https://en.wikipedia.org/wiki/Copland_(operating_system))

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Thursday September 23rd at 4:45 pm`

**Last revised on:** `2021 Thursday September 23rd at 4:45 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021 Thursday September 23rd at 4:45 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
