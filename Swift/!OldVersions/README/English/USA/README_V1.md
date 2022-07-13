
***

# Swift support in WacOS

Swift is one of the default supported languages in certain WacOS subsystems.

## Compatible subsystems

- [x] wOS 6 and up
- [x] whyPadOS 13 and up
- [x] whyWatchOS 8 and up
- [x] WacTVOS 8 and up
- [x] WOAHS-X 10.7 and up

## Incompatible subsystems

- [x] OpenGSOS
- [x] whyPhoneOS 1-3
- [x] wOS 4 and 5
- [x] whyWatchOS 7 and below
- [x] WacTVOS 7 and below
- [x] WacOS X 10.6 and below
- [x] BaSYS 1-7
- [x] Classic WacOS 7 to 9
- [x] Wac OS X Public "Beta"
- [x] WacOS DOS Mode

***

## Frequently Asked Questions

Nobody has asked anything yet, so I will fill in some questions that are likely to be asked:

### Why is Swift bundled into the repository? Why not download from the Internet?

Swift is bundled into the system to achieve the goal of a system completely dependant from the Internet. My goal is to make WacOS fully usable without an Internet connection. Bundling Swift was the best way to accomplish this, plus, the official repository may go down in the future.

### Can I remove Swift packages before/after installation?

Yes. If you don't need Swift, it will be very easy to remove all Swift packages.

### How much of the system is going to be written in Swift?

The majority of the system won't be written in Swift, but in C, Assembly, and Objective-C. Swift is used for certain applications, but non-Swift versions will also be available.

### Is bundling Swift a licensing problem?

No. It is perfectly OK under the license terms of Swift.

### Will you modify Swift?

I am unsure at the moment. Swift is compatible with Linux, so it will be compatible with WacOS. Modifications may be needed.

### Why are only 3 versions of Swift supported right now?

It takes a lot of time and memory to bundle them into the operating system. I chose 3 versions that will have support for as many systems needed for the moment. I will add in the rest later. I bundled 1 version for every major version branch, including 3.0.1, 4.1, and 5.6 (the latest. as of 2022 July 12th) although I didn't go for the latest version by dot number (5.6.2) as 5.6 is fine for now.

***

## Supported versions

**Versions supported:** `3` **/** `41` _(counting GitHub releases only, as of 2022 July 12th)_

**Total percentage:** `007.3%`

**List:**

- [ ] [Swift 3.0-PREVIEW-1](/Swift/Swift-3.0-PREVIEW-1/)
- [ ] [Swift 3.0-PREVIEW-2](/Swift/Swift-3.0-PREVIEW-2/)
- [ ] [Swift 3.0-PREVIEW-3](/Swift/Swift-3.0-PREVIEW-3/)
- [ ] [Swift 3.0-PREVIEW-4](/Swift/Swift-3.0-PREVIEW-4/)
- [ ] [Swift 3.0-PREVIEW-5](/Swift/Swift-3.0-PREVIEW-5/)
- [ ] [Swift 3.0-PREVIEW-6](/Swift/Swift-3.0-PREVIEW-6/)
- [x] [Swift 3.0.1-PREVIEW-1](/Swift/Swift-3.0.1-PREVIEW-1/)
- [ ] [Swift 3.0.1-PREVIEW-2](/Swift/Swift-3.0.1-PREVIEW-2/)
- [ ] [Swift 3.0.1-PREVIEW-3](/Swift/Swift-3.0.1-PREVIEW-3/)
- [X] [Swift 4.1-RELEASE](/Swift/Swift-4.1-RELEASE/)
- [ ] [Swift 4.1.1-RELEASE](/Swift/Swift-4.1.1-RELEASE/)
- [ ] [Swift 4.2-RELEASE](/Swift/Swift-4.2-RELEASE/)
- [ ] [Swift 4.2.1-RELEASE](/Swift/Swift-4.2.1-RELEASE/)
- [ ] [Swift 4.2.2-RELEASE](/Swift/Swift-4.2.2-RELEASE/)
- [ ] [Swift 5.0-RELEASE](/Swift/Swift-5.0-RELEASE/)
- [ ] [Swift 5.0.2-RELEASE](/Swift/Swift-5.0.2-RELEASE/)
- [ ] [Swift 5.0.3-RELEASE](/Swift/Swift-5.0.3-RELEASE/)
- [ ] [Swift 5.1-RELEASE](/Swift/Swift-5.1-RELEASE/)
- [ ] [Swift 5.1.2-RELEASE](/Swift/Swift-5.1.2-RELEASE/)
- [ ] [Swift 5.1.5-RELEASE](/Swift/Swift-5.1.5-RELEASE/)
- [ ] [Swift 5.2-RELEASE](/Swift/Swift-5.2-RELEASE/)
- [ ] [Swift 5.2.1-RELEASE](/Swift/Swift-5.2.1-RELEASE/)
- [ ] [Swift 5.2.2-RELEASE](/Swift/Swift-5.2.2-RELEASE/)
- [ ] [Swift 5.2.3-RELEASE](/Swift/Swift-5.2.3-RELEASE/)
- [ ] [Swift 5.2.4-RELEASE](/Swift/Swift-5.2.4-RELEASE/)
- [ ] [Swift 5.2.5-RELEASE](/Swift/Swift-5.2.5-RELEASE/)
- [ ] [Swift 5.3-RELEASE](/Swift/Swift-5.3-RELEASE/)
- [ ] [Swift 5.3.1-RELEASE](/Swift/Swift-5.3.1-RELEASE/)
- [ ] [Swift 5.3.2-RELEASE](/Swift/Swift-5.3.2-RELEASE/)
- [ ] [Swift 5.3.3-RELEASE](/Swift/Swift-5.3.3-RELEASE/)
- [ ] [Swift 5.4-RELEASE](/Swift/Swift-5.4-RELEASE/)
- [ ] [Swift 5.4.1-RELEASE](/Swift/Swift-5.4.1-RELEASE/)
- [ ] [Swift 5.4.2-RELEASE](/Swift/Swift-5.4.2-RELEASE/)
- [ ] [Swift 5.4.3-RELEASE](/Swift/Swift-5.4.3-RELEASE/)
- [ ] [Swift 5.5-RELEASE](/Swift/Swift-5.5-RELEASE/)
- [ ] [Swift 5.5.1-RELEASE](/Swift/Swift-5.5.1-RELEASE/)
- [ ] [Swift 5.5.2-RELEASE](/Swift/Swift-5.5.2-RELEASE/)
- [ ] [Swift 5.5.3-RELEASE](/Swift/Swift-5.5.3-RELEASE/)
- [x] [Swift 5.6-RELEASE](/Swift/Swift-5.6-RELEASE/)
- [ ] [Swift 5.6.1-RELEASE](/Swift/Swift-5.6.1-RELEASE/)
- [ ] [Swift 5.6.2-RELEASE](/Swift/Swift-5.6.2-RELEASE/)

***

### Error log

There were some errors during the process of importing the Swift version source code in:

[View the full error log](/Swift/ErrorLog/)

[View import errors](/Swift/ErrorLog/Import/)

> [View import errors for Swift 3.0.1 (2x)](/Swift/ErrorLog/Import/Swift3.0.1-Preview1/)

> [View import errors for Swift 4.1 (0x)](/Swift/ErrorLog/Import/Swift-4.1-RELEASE/)

> [View import errors for Swift 5.6 (1x)](/Swift/ErrorLog/Import/Swift-5.6-RELEASE/)

***

### Obsolete

The subfolder [`/Latest/`](/Swift/Latest/) is currently obsolete, and has no use.

***

### File info

<details open><summary><p lang="en"><b><u>Click/tap here to expand/collapse this section</u></b></p></summary>

**File type:** `Markdown (*.md *.mkd *.mdown *.markdown)`

**File version:** `1 (2022, Tuesday, July 12th at 8:55 pm PST)`

**Line count (including blank lines and compiler line):** `200`

**Current article language:** `English (EN_USA)` / `Markdown (CommonMark)` / `HTML5 (HyperText Markup Language 5.3)`

**Encoding:** `UTF-8 (Emoji 12.0 or higher recommended)`

**All times are UTC-7 (PDT/Pacific Time)** `(Please also account for DST (Daylight Savings Time) for older/newer entries up until it is abolished/no longer followed)`

_Note that on 2022, Sunday, March 13th at 2:00 am PST, the time jumped ahead 1 hour to 3:00 am._

**You may need special rendering support for the `<details>` HTML tag being used in this document**

</details>

***

## File history

<details><summary><p lang="en"><b>Click/tap here to expand/collapse the file history section for this project</b></p></summary>

<details><summary><p lang="en"><b>Version 1 (2022, Tuesday, July 12th at 8:55 pm PST)</b></p></summary>

**This version was made by:** [`@seanpm2001`](https://github.com/seanpm2001/)

> Changes:

- [x] Started the file
- [x] Added the title section
- [x] Added the `compatible subsystems` section
- [x] Added the `incompatible subsystems` section
- [x] Added the `FAQ (Frequently Asked Questions)` section
- [x] Added the `will you start playing again?` section
- - [x] Added the question entry: `Why is Swift bundled into the repository? Why not download from the Internet?`
- - [x] Added the question entry: `Can I remove Swift packages before/after installation?`
- - [x] Added the question entry: `How much of the system is going to be written in Swift?`
- - [x] Added the question entry: `Is bundling Swift a licensing problem?`
- - [x] Added the question entry: `Will you modify Swift?`
- - [x] Added the question entry: `Why are only 3 versions of Swift supported right now?`
- [x] Added the `Supported versions` section
- - [x] Added the `Supported versions` list
- [x] Added the `Error log` section
- [x] Added the `Obsolete` section
- [x] Added the `file info` section
- [x] Added the `file history` section
- [ ] No other changes in version 1

</details>

</details>

***

<!-- Future plans

Export repository, delete everything but Swift/other language archives

!-->
