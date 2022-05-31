
***

<details><summary><b lang="en">Click/tap here to expand/collapse the logo for this subproject</b></summary>

![W_Aquatic.png](W_Aquatic.png)

</details>

# WacOS X Public "Beta"

Wac OS X Public "Beta" is an open source recreation of Mac OS X Public Beta. It is part of the WacOS operating system project. 

## Beta

The software is in Alpha phase. When it reaches the beta phase, development will still continue. In this rare instance, the term "Beta" is false for its development, and is done to replicate the original project.

## Language

The system is currently written in C, but will also support several other languages, including x86 Assembly, Objective-C, and AppleScript

## Features

Features to replicate

Everything in Wac OS X 10.0, but less developed

Source: [Mac OS X 10.0 - Wikipedia (en)](https://en.wikipedia.org/w/index.php?title=Mac_OS_X_10.0&oldid=1086308900)

* Dock — the Dock was a new way of organizing one's Mac OS X applications on a user interface, and a change from the classic method of Application launching in previous Mac OS systems.

* OSFMK 7.3 — the Open Software Foundation Mach kernel from the OSF was part of the XNU kernel for Mac OS X, and was one of the largest changes from a technical standpoint in Mac OS X.

* Terminal — the Terminal was a feature that allowed access to Mac OS X's underpinnings, namely the Unix core. Mac OS had previously had the distinction of being one of the few operating systems with no command line interface at all.

* Mail — email client.

* Address Book

* TextEdit — new on-board word processor, replacement to SimpleText.

* Full preemptive multitasking support, a long-awaited feature on the Mac.

* PDF Support (create PDFs from any application)

* Aqua UI — new user interface

* Built on Darwin, a Unix-like operating system.

* OpenGL

* AppleScript

* Support for Carbon and Cocoa APIs

* Sherlock — desktop and web search engine.

* Protected memory — memory protection so that if an application corrupts its memory, the memory of other applications will not be corrupted.

Additionally:

* Rotten apple mode (a configuration tweak that disables the GUI on the set date)

Lines 4-12 are shown here of [PBCONFIG.cfg](PBCONFIG.cfg)

```ini
; Rotten apple
; The original Mac OS X Public Beta was set to expire on 2001 May 14th
; Just like its predecessor, the operating system can expire (the GUI mode) however, for this version, it can be turned off more easily
; (by default, it is not set to expire)

Expiration_date_YEAR = "2001"
Expiration_date_MONTH = "5"
Expiration_date_DAY = "14"
ExpireMode = "False"
```

Change the last line to this:

```ini
ExpireMode = "True"
```

To enable Rotten Apple mode.

## Home repositories

[Guesthouse repository](https://github.com/seanpm2001/WacOS_X_Public_-Beta-/)

This is a guesthouse repository, and not a home repository, as development mainly stays on the main WacOS side. This is just the guesthouse that the project retreats to at times. If you are already in this repository, the link is likely recursive, and will reload the page.

[Home repository](https://github.com/seanpm2001/WacOS/tree/WacOS-dev/WacOS_X/Public_Beta/)

This is the home repository. If you are already in this repository, the link is likely recursive, and will reload the page.

***

## File info

**File type:** `Markdown document (*.md *.mkd *.mdown *.markdown)`

**File version:** `1 (2022, Monday, May 30th at 7:12 pm PST)`

**Line count (including blank lines and compiler line):** `109`

**Current article language:** `English (US)`

***
