
***

# C (Programming language)

[Go back (WacOS/Wiki/)](https://github.com/seanpm2001/WacOS/wiki)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/The_C_Programming_Language_logo.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/The_C_Programming_Language_logo.svg)

**This entire section was copied and slightly mofified from Wikipedia and needs to be rewritten.**

C (/ˈsiː/, as in the letter c) is a general-purpose, procedural computer programming language supporting structured programming, lexical variable scope, and recursion, with a static type system. By design, C provides constructs that map efficiently to typical machine instructions. It has found lasting use in applications previously coded in assembly language. Such applications include operating systems and various application software for computer architectures that range from supercomputers to PLCs and embedded systems.

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/Ken_Thompson_and_Dennis_Ritchie--1973.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/Ken_Thompson_and_Dennis_Ritchie--1973.jpg)

A successor to the programming language B, C was originally developed at Bell Labs by [Dennis Ritchie](https://github.com/seanpm2001/WacOS/wiki/Dennis_Ritchie/) between 1972 and 1973 to construct utilities running on Unix. It was applied to re-implementing the kernel of the Unix operating system. During the 1980s, C gradually gained popularity. It has become one of the most widely used programming languages, with C compilers from various vendors available for the majority of existing computer architectures and operating systems. C has been standardized by ANSI since 1989 (ANSI C) and by the International Organization for Standardization (ISO).

C is an imperative procedural language. It was designed to be compiled to provide low-level access to memory and language constructs that map efficiently to machine instructions, all with minimal runtime support. Despite its low-level capabilities, the language was designed to encourage cross-platform programming. A standards-compliant C program written with portability in mind can be compiled for a wide variety of computer platforms and operating systems with few changes to its source code.

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/Tiobe_index_2020_may.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/Tiobe_index_2020_may.png)

As of January 2021, C was ranked first in the TIOBE index, a measure of the popularity of programming languages, moving up from the no. 2 spot the previous year.

***

## Book

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/The_C_Programming_Language%2C_First_Edition_Cover.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/The_C_Programming_Language%2C_First_Edition_Cover.svg)

The C programming language had its own book written about its usage, the first book being written on the language. The book is commonly referred to as K&R C (the K & R standing for Ken and Ritchie, the languages main developers)

***

## Examples

The following chapter will list examples of usage of the language. There currently are only 3 examples.

### Long keyword

In early versions of C, only functions that return types other than int must be declared if used before the function definition; functions used without prior declaration were presumed to return type int.

For example:

```c
long some_function();
/* int */ other_function();

/* int */ calling_function()
{
    long test1;
    register /* int */ test2;

    test1 = some_function();
    if (test1 > 0)
          test2 = 0;
    else
          test2 = other_function();
    return test2;
}
```

### Hello World

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/Hello_World_Brian_Kernighan_1978.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C/Hello_World_Brian_Kernighan_1978.jpg)

The Hello World program originated with the C programming language. It was designed by Brian Kernighan in 1978. His example is listed above.

The original C Hello World program is written like so:

```c
main()
{
    printf("hello, world\n");
}
```

However, this is not a conforming version. Here is a standard conforming version of the Hello World program.

```c
# include <stdio.h>

int main(void)
{
    printf("hello, world\n");
}
```

***

## Use by Apple

Apple used the language to create [Objective-C](https://github.com/seanpm2001/WacOS/wiki/Objective-C/), and C partially inspired the Swift language, but I currently don't see much other use of it by Apple.

## Use by WacOS

WacOS uses C for most system functions, but tries to also use other languages to promote software diversity. 

_This article on programming languages is a stub. You can help by expanding it!_

WacOS also uses [C++](https://github.com/seanpm2001/WacOS/wiki/CPP/) in some areas for the same reasons.

***

## Sources

[Source: Wikipedia - C (programming language)](https://en.wikipedia.org/wiki/C_(programming_language))

More sources needed, Wikipedia cannot be the only source, as Wikipedia isn't really something to use as a source. More credible sources are needed.

***

## Article info

**Written on:** `2021, Monday, September 20th at 4:08 pm)`

**Last revised on:** `2021, Monday, September 20th at 4:08 pm)`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Monday, September 20th at 4:08 pm)`

***
