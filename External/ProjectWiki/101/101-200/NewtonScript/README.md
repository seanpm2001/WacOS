  
***

# NewtonScript

( **Derived from:** [Self](https://github.com/seanpm2001/WacOS/wiki/Self/) and or [C++/unknown](https://github.com/seanpm2001/WacOS/wiki/C-Plus-Plus/) | **Platform:** [NewtonOS](https://github.com/seanpm2001/WacOS/wiki/NewtonOS/) )

NewtonScript is a prototype-based programming language created to write programs for the Newton platform. It is heavily influenced by the [Self](https://github.com/seanpm2001/WacOS/wiki/Self/) programming language, but modified to be more suited to needs of mobile and embedded devices.

## History

On August 3, [1993](https://github.com/seanpm2001/WacOS/wiki/1993/), Apple unveiled Apple Newton MessagePad. The device had 640 KB RAM and 4 MB ROM, running on 20 MHz Acorn RISC machine.

The main intention behind Newton project, was to develop a device capable of replacing a computer while being portable. With limited battery and memory, the developers were looking for programming language capable of meeting these challenges.

The developers looked at [C++](https://github.com/seanpm2001/WacOS/wiki/C-Plus-Plus/) language but realized that it lacked flexibility. They started focusing on prototype based languages and were impressed with Smalltalk and [Self](https://github.com/seanpm2001/WacOS/wiki/Self/). Concurrently Apple was developing another dynamic programming language called Dylan, which was a strong candidate for Newton platform.

However, both Self and Dylan were dropped out of consideration, as they were both in nascent stage for proper integration.

Instead, a team headed by Walter R Smith, developed a new language called as NewtonScript. it was influenced by dynamic language like Smalltalk and prototype model based like Self.

## Features

Although NewtonScript was heavily influenced by Self, there were some differences in both the languages.

Differences arose due to three perceived problems with Self.

One is that the typical Self snapshot requires 32 MB of RAM to run in, whereas the Newton platform was designed to use only 128 KB for the operating system. This required some serious paring down of the engine to make it fit and still have room for applications.

Another issue was performance. Since the language would be used for the entire system, as opposed to just running on an existing operating system, it needed to run as fast as possible.

Finally, the inheritance system in the normal Self engine had a single parent object, whereas GUIs typically have two — one for the objects and another for the GUI layout that is typically handled via the addition of a slot in some sort of GUI-hierarchy object (like View).

The syntax was also modified to allow a more text-based programming style, as opposed to Self's widespread use of a GUI environment for programming. This allowed Newton programs to be developed on a computer running the Toolkit, where the programs would be compiled and then downloaded to a Newton device for running.

One of the advantages of NewtonScript's prototype based inheritance was reduced memory usage, a key consideration in the 128 KB Newton. The prototype of a GUI object could actually be stored in ROM, so there was no need to copy default data or functions into working memory.

Unlike class based languages, where creation of an object involves memory being allocated to all of its attributes, NewtonScripts' use of prototype inheritance allowed it to allocated memory to few fields like _proto and _parent instead of creating whole new object. Here, _proto and _parent signifies whether the object is using prototype or parent inheritance.

An example to illustrate above concept, a developer might create a new button instance. If the button uses the default font, accessing its font "slot" (i.e., property or member variable) will return a value that is actually stored in ROM; the button instance in RAM does not have a value in its own font slot, so the prototype inheritance chain is followed until a value is found. If the developer then changes the button's font, setting its font slot to a new value will override the prototype; this override value is stored in RAM. NewtonScript's "differential inheritance" therefore made efficient use of the Newton's expensive flash RAM by storing the bulk of the default data and code in the PDA's cheaper and much larger ROM.

## Important terms

Views: They are objects created by Newton View System, which are created on run-time to render views.

Template: It is a blueprint from which views are created.

Protos: They can be blueprint for a template or a view, and are elements of NewtonScript code libraries.

Frame and Slot: Frame is a dynamic collection of slots, and one element of frame is called as a slot. A slot is made up of name and value. The value can be of any type. It is worthwhile to note that all objects in NewtonScript are frames.

Soup and Entry: It is a related collection of frames/ data. Entry is an individual frame in a soup.

Frame Heap: RAM allocated dynamically by NewtonScript.

Base View: It is the main view of application, consisting of all the variables and methods used in the application.
Advantages and Disadvantages

## Advantages

NewtonScript is a dynamic prototype based programming language, which uses differential inheritance. This means that it is very effective is using memory space. Being dynamic, it is easy to modify objects, type checking, etc. at run time, giving great flexibility for developers.

Objects created can be stored in permanent memory like flash card or internal memory. The RAM is used only for storing attributes whose values change during runtime. This reduces memory consumption.

Writing interfaces for GUI applications can be implemented effectively using the prototype model, since we can directly write an object for a GUI control rather than creating a class and instantiating it.

Garbage collection is carried automatically by the system. This helped the developers to focus more on application development rather than worrying about memory management. Garbage collection also helped in mitigating problem of dangling pointers where a pointer erroneously points to a memory location that was deallocated.

## Disadvantages

Since NewtonScript code was written on one platform and run on another, it was practically impossible to debug. Better debugging code in the Newton engine would have helped offset this problem to some degree, but the limited memory made this difficult. Instead the developer would get a vague indication along with an error code, without any ability to match it to lines in the original code.

Another disadvantage is that, dynamic variable reduces the speed of operation since simple pointer dereference cannot be used as in statically typed like C++ and Java.

## Influences

With the cancellation of Newton project by Apple in 1998, all further mainstream developments on NewtonScript were stopped. However, the features used in NewtonScript would continue to inspire other programming models and languages.

The prototype-based object model of Self and NewtonScript was used in JavaScript, the most popular and visible language to use the concept so far.

NewtonScript is also one of the conceptual ancestors (together with Smalltalk, Self, Act1, Lisp and Lua) of a general-purpose programming language called Io which implements the same differential inheritance model, which was used in NewtonScript to conserve memory. 

**This article is a modified copy of its Wikipedia counterpart and needs to be rewritten for originality.**

***

## Sources

[Wikipedia - NewtonScript](https://en.wikipedia.org/wiki/NewtonScript/)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being half a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Sunday September 26th at 4:37 pm`

**Last revised on:** `2021 Sunday September 26th at 2:26 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article language:** `English (US) / Markdown`

**Line count (including blank lines and compiler line):** `118`

**Article version:** `1 (2021 Sunday September 26th at 4:37 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
