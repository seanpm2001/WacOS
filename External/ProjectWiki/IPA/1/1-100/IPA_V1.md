
***

# IPA

## iPhone App Store package

The iPhone App Store package (*.ipa) is a file format introduced in [iPhone OS 2](https://github.com/seanpm2001/WacOS/wiki/iPhoneOS-2/) that lets users download and install apps onto their iPhones, iPods, and iPads. Most .ipa files cannot be installed on the iPhone Simulator because they do not contain a binary for the x86 architecture, only one for the ARM architecture of mobile phones. The file format can also be used to install programs on MacOS and tvOS via their App Store equivalents.

## Built-in structure

An IPA has a built-in structure for iTunes and App Store to recognize. The example below shows the structure of an IPA:

```ipa
/Payload/
/Payload/Application.app/
/iTunesArtwork
/iTunesArtwork@2x
/iTunesMetadata.plist
/WatchKitSupport/WK
/META-INF
```

## Jailbreaking

An unsigned .ipa can be created by copying the folder with the extension .app from the Products folder of the application in Xcode to a folder called Payload and compressing the latter using the command zip -0 -y -r myAppName.ipa Payload/.

It is then possible to install unsigned .ipa files on iOS jailbroken devices using third party software. AppSync is the tool for installing such homebrew apps. Similar to the case of game console hacking, people are known to use this installation for piracy, against the tool developer's wishes: some underground communities form around buying an app and then sharing its DRM-free unsigned version. 

## In WacOS

The WacOS project is working on a way to reverse engineer IPA and DMG files, so that they can natively run on WacOS (Linux) and WacOS (XNU) it is one of the major features planned for this distribution.

***

## Sources

[Wikipedia](https://en.wikipedia.org/wiki/.ipa)

More sources needed, Wikipedia should not be the only source. Wikipedia is not supposed to be used as a source, and other sources are needed.

***

## Article info

**Written on:** `2021 Sunday September 19th at 1:38 pm`

**Last revised on:** `2021 Sunday September 19th at 1:38 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021 Sunday September 19th at 1:38 pm)`

***
