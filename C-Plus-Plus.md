
***

# C++ (programming language)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/ISO_C%2B%2B_Logo.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/ISO_C%2B%2B_Logo.svg)

( **Influenced by:** `Ada`, `ALGOL 68`, `C`, `CLU`, `ML`, `Mesa`, `Modula-2`, `Simula`, `Smalltalk` | **Influenced:** `Ada 95`, `C#`, `C99`, `Chapel`, `Clojure`, `D`, `Java`, `JS++`, `Lua`, `Nim`, `Objective-C++`, `Perl`, `PHP`, `Python`, `Rust`, `Seed7`)


C++ (/ˌsiːˌplʌsˈplʌs/) is a general-purpose programming language created by Bjarne Stroustrup as an extension of the C programming language, or "C with Classes". The language has expanded significantly over time, and modern C++ now has object-oriented, generic, and functional features in addition to facilities for low-level memory manipulation. It is almost always implemented as a compiled language, and many vendors provide C++ compilers, including the Free Software Foundation, LLVM, Microsoft, Intel, Oracle, and IBM, so it is available on many platforms.

C++ was designed with an orientation toward system programming and embedded, resource-constrained software and large systems, with performance, efficiency, and flexibility of use as its design highlights. C++ has also been found useful in many other contexts, with key strengths being software infrastructure and resource-constrained applications, including desktop applications, video games, servers (e.g. e-commerce, web search, or databases), and performance-critical applications (e.g. telephone switches or space probes).

C++ is standardized by the International Organization for Standardization (ISO), with the latest standard version ratified and published by ISO in December 2020 as ISO/IEC 14882:2020 (informally known as C++20). The C++ programming language was initially standardized in 1998 as ISO/IEC 14882:1998, which was then amended by the C++03, C++11, C++14, and C++17 standards. The current C++20 standard supersedes these with new features and an enlarged standard library. Before the initial standardization in 1998, C++ was developed by Danish computer scientist Bjarne Stroustrup at Bell Labs since 1979 as an extension of the C language; he wanted an efficient and flexible language similar to C that also provided high-level features for program organization. Since 2012, C++ has been on a three-year release schedule with C++23 as the next planned standard.

## History

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/BjarneStroustrup.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/BjarneStroustrup.jpg)

_Bjarne Stroustrup, the creator of C++, in his AT&T New Jersey office c. 2000_

In 1979, Bjarne Stroustrup, a Danish computer scientist, began work on "C with Classes", the predecessor to C++. The motivation for creating a new language originated from Stroustrup's experience in programming for his PhD thesis. Stroustrup found that Simula had features that were very helpful for large software development, but the language was too slow for practical use, while BCPL was fast but too low-level to be suitable for large software development. When Stroustrup started working in AT&T Bell Labs, he had the problem of analyzing the UNIX kernel with respect to distributed computing. Remembering his PhD experience, Stroustrup set out to enhance the C language with Simula-like features. C was chosen because it was general-purpose, fast, portable and widely used. As well as C and Simula's influences, other languages also influenced this new language, including ALGOL 68, Ada, CLU and ML.

Initially, Stroustrup's "C with Classes" added features to the C compiler, Cpre, including classes, derived classes, strong typing, inlining and default arguments.

In 1982, Stroustrup started to develop a successor to C with Classes, which he named "C++" (++ being the increment operator in C) after going through several other names. New features were added, including virtual functions, function name and operator overloading, references, constants, type-safe free-store memory allocation (new/delete), improved type checking, and BCPL style single-line comments with two forward slashes (//). Furthermore, Stroustrup developed a new, standalone compiler for C++, Cfront.

In 1984, Stroustrup implemented the first stream input/output library. The idea of providing an output operator rather than a named output function was suggested by Doug McIlroy (who had previously suggested Unix pipes).

In 1985, the first edition of The C++ Programming Language was released, which became the definitive reference for the language, as there was not yet an official standard. The first commercial implementation of C++ was released in October of the same year.

In 1989, C++ 2.0 was released, followed by the updated second edition of The C++ Programming Language in 1991. New features in 2.0 included multiple inheritance, abstract classes, static member functions, const member functions, and protected members. In 1990, The Annotated C++ Reference Manual was published. This work became the basis for the future standard. Later feature additions included templates, exceptions, namespaces, new casts, and a Boolean type.

In 1998, C++98 was released, standardizing the language, and a minor update (C++03) was released in 2003.

After C++98, C++ evolved relatively slowly until, in 2011, the C++11 standard was released, adding numerous new features, enlarging the standard library further, and providing more facilities to C++ programmers. After a minor C++14 update released in December 2014, various new additions were introduced in C++17. After becoming finalized in February 2020, a draft of the C++20 standard was approved on 4 September 2020 and officially published on 15 December 2020.

On January 3, 2018, Stroustrup was announced as the 2018 winner of the Charles Stark Draper Prize for Engineering, "for conceptualizing and developing the C++ programming language".

As of 2021 C++ ranked fourth on the TIOBE index, a measure of the popularity of programming languages, after C, Java, and Python.

## Etymology

According to Stroustrup, "the name signifies the evolutionary nature of the changes from C". This name is credited to Rick Mascitti (mid-1983) and was first used in December 1983. When Mascitti was questioned informally in 1992 about the naming, he indicated that it was given in a tongue-in-cheek spirit. The name comes from C's ++ operator (which increments the value of a variable) and a common naming convention of using "+" to indicate an enhanced computer program.

During C++'s development period, the language had been referred to as "new C" and "C with Classes" before acquiring its final name.

## Philosophy

Throughout C++'s life, its development and evolution has been guided by a set of principles:

It must be driven by actual problems and its features should be immediately useful in real world programs.

Every feature should be implementable (with a reasonably obvious way to do so).

Programmers should be free to pick their own programming style, and that style should be fully supported by C++.

Allowing a useful feature is more important than preventing every possible misuse of C++.

It should provide facilities for organising programs into separate, well-defined parts, and provide facilities for combining separately developed parts.

No implicit violations of the type system (but allow explicit violations; that is, those explicitly requested by the programmer).

User-created types need to have the same support and performance as built-in types.

Unused features should not negatively impact created executables (e.g. in lower performance).

There should be no language beneath C++ (except assembly language).

C++ should work alongside other existing programming languages, rather than fostering its own separate and incompatible programming environment.

If the programmer's intent is unknown, allow the programmer to specify it by providing manual control.

## Standardization

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/C%2B%2B_Standards_Committee_meeting_-_July_1996_Stockholm_-_Wednesday_general_session.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/C%2B%2B_Standards_Committee_meeting_-_July_1996_Stockholm_-_Wednesday_general_session.jpg)

Scene during the C++ Standards Committee meeting in Stockholm in 1996

C++ standards Year 	C++ Standard 	Informal name

1998 	ISO/IEC 14882:1998 	C++98

2003 	ISO/IEC 14882:2003 	C++03

2011 	ISO/IEC 14882:2011 	C++11, C++0x

2014 	ISO/IEC 14882:2014 	C++14, C++1y

2017 	ISO/IEC 14882:2017 	C++17, C++1z

2020 	ISO/IEC 14882:2020 	C++20, C++2a

C++ is standardized by an ISO working group known as JTC1/SC22/WG21. So far, it has published six revisions of the C++ standard and is currently working on the next revision, C++23.

In 1998, the ISO working group standardized C++ for the first time as ISO/IEC 14882:1998, which is informally known as C++98. In 2003, it published a new version of the C++ standard called ISO/IEC 14882:2003, which fixed problems identified in C++98.

The next major revision of the standard was informally referred to as "C++0x", but it was not released until 2011. C++11 (14882:2011) included many additions to both the core language and the standard library.

In 2014, C++14 (also known as C++1y) was released as a small extension to C++11, featuring mainly bug fixes and small improvements. The Draft International Standard ballot procedures completed in mid-August 2014.

After C++14, a major revision C++17, informally known as C++1z, was completed by the ISO C++ Committee in mid July 2017 and was approved and published in December 2017.

As part of the standardization process, ISO also publishes technical reports and specifications:

ISO/IEC TR 18015:2006 on the use of C++ in embedded systems and on performance implications of C++ language and library features,

ISO/IEC TR 19768:2007 (also known as the C++ Technical Report 1) on library extensions mostly integrated into C++11,

ISO/IEC TR 29124:2010 on special mathematical functions, integrated into C++17

ISO/IEC TR 24733:2011 on decimal floating-point arithmetic,

ISO/IEC TS 18822:2015 on the standard filesystem library, integrated into C++17

ISO/IEC TS 19570:2015 on parallel versions of the standard library algorithms, integrated into C++17

ISO/IEC TS 19841:2015 on software transactional memory,

ISO/IEC TS 19568:2015 on a new set of library extensions, some of which are already integrated into C++17,

ISO/IEC TS 19217:2015 on the C++ concepts, integrated into C++20

ISO/IEC TS 19571:2016 on the library extensions for concurrency, some of which are already integrated into C++20

ISO/IEC TS 19568:2017 on a new set of general-purpose library extensions

ISO/IEC TS 21425:2017 on the library extensions for ranges, integrated into C++20

ISO/IEC TS 22277:2017 on coroutines, integrated into C++20

ISO/IEC TS 19216:2018 on the networking library

ISO/IEC TS 21544:2018 on modules, integrated into C++20

ISO/IEC TS 19570:2018 on a new set of library extensions for parallelism

More technical specifications are in development and pending approval, including static reflection.

## Language

The C++ language has two main components: a direct mapping of hardware features provided primarily by the C subset, and zero-overhead abstractions based on those mappings. Stroustrup describes C++ as "a light-weight abstraction programming language [designed] for building and using efficient and elegant abstractions"; and "offering both hardware access and abstraction is the basis of C++. Doing it efficiently is what distinguishes it from other languages."

C++ inherits most of C's syntax. The following is Bjarne Stroustrup's version of the Hello world program that uses the C++ Standard Library stream facility to write a message to standard output:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, world!\n";
}
```

## Object storage

As in C, C++ supports four types of memory management: static storage duration objects, thread storage duration objects, automatic storage duration objects, and dynamic storage duration objects.

### Static storage duration objects

Static storage duration objects are created before main() is entered (see exceptions below) and destroyed in reverse order of creation after main() exits. The exact order of creation is not specified by the standard (though there are some rules defined below) to allow implementations some freedom in how to organize their implementation. More formally, objects of this type have a lifespan that "shall last for the duration of the program".

Static storage duration objects are initialized in two phases. First, "static initialization" is performed, and only after all static initialization is performed, "dynamic initialization" is performed. In static initialization, all objects are first initialized with zeros; after that, all objects that have a constant initialization phase are initialized with the constant expression (i.e. variables initialized with a literal or constexpr). Though it is not specified in the standard, the static initialization phase can be completed at compile time and saved in the data partition of the executable. Dynamic initialization involves all object initialization done via a constructor or function call (unless the function is marked with constexpr, in C++11). The dynamic initialization order is defined as the order of declaration within the compilation unit (i.e. the same file). No guarantees are provided about the order of initialization between compilation units.

### Thread storage duration objects

Variables of this type are very similar to static storage duration objects. The main difference is the creation time is just prior to thread creation and destruction is done after the thread has been joined.

### Automatic storage duration objects

The most common variable types in C++ are local variables inside a function or block, and temporary variables. The common feature about automatic variables is that they have a lifetime that is limited to the scope of the variable. They are created and potentially initialized at the point of declaration (see below for details) and destroyed in the reverse order of creation when the scope is left. This is implemented by allocation on the stack.

Local variables are created as the point of execution passes the declaration point. If the variable has a constructor or initializer this is used to define the initial state of the object. Local variables are destroyed when the local block or function that they are declared in is closed. C++ destructors for local variables are called at the end of the object lifetime, allowing a discipline for automatic resource management termed RAII, which is widely used in C++.

Member variables are created when the parent object is created. Array members are initialized from 0 to the last member of the array in order. Member variables are destroyed when the parent object is destroyed in the reverse order of creation. i.e. If the parent is an "automatic object" then it will be destroyed when it goes out of scope which triggers the destruction of all its members.

Temporary variables are created as the result of expression evaluation and are destroyed when the statement containing the expression has been fully evaluated (usually at the ; at the end of a statement).

### Dynamic storage duration objects

These objects have a dynamic lifespan and can be created directly with a call to new and destroyed explicitly with a call to delete. C++ also supports malloc and free, from C, but these are not compatible with new and delete. Use of new returns an address to the allocated memory. The C++ Core Guidelines advise against using new directly for creating dynamic objects in favor of smart pointers through make_unique for single ownership and make_shared for reference-counted multiple ownership, which were introduced in C++11.

## Templates

C++ templates enable generic programming. C++ supports function, class, alias, and variable templates. Templates may be parameterized by types, compile-time constants, and other templates. Templates are implemented by instantiation at compile-time. To instantiate a template, compilers substitute specific arguments for a template's parameters to generate a concrete function or class instance. Some substitutions are not possible; these are eliminated by an overload resolution policy described by the phrase "Substitution failure is not an error" (SFINAE). Templates are a powerful tool that can be used for generic programming, template metaprogramming, and code optimization, but this power implies a cost. Template use may increase code size, because each template instantiation produces a copy of the template code: one for each set of template arguments, however, this is the same or smaller amount of code that would be generated if the code was written by hand. This is in contrast to run-time generics seen in other languages (e.g., Java) where at compile-time the type is erased and a single template body is preserved.

Templates are different from macros: while both of these compile-time language features enable conditional compilation, templates are not restricted to lexical substitution. Templates are aware of the semantics and type system of their companion language, as well as all compile-time type definitions, and can perform high-level operations including programmatic flow control based on evaluation of strictly type-checked parameters. Macros are capable of conditional control over compilation based on predetermined criteria, but cannot instantiate new types, recurse, or perform type evaluation and in effect are limited to pre-compilation text-substitution and text-inclusion/exclusion. In other words, macros can control compilation flow based on pre-defined symbols but cannot, unlike templates, independently instantiate new symbols. Templates are a tool for static polymorphism (see below) and generic programming.

In addition, templates are a compile-time mechanism in C++ that is Turing-complete, meaning that any computation expressible by a computer program can be computed, in some form, by a template metaprogram prior to runtime.

In summary, a template is a compile-time parameterized function or class written without knowledge of the specific arguments used to instantiate it. After instantiation, the resulting code is equivalent to code written specifically for the passed arguments. In this manner, templates provide a way to decouple generic, broadly applicable aspects of functions and classes (encoded in templates) from specific aspects (encoded in template parameters) without sacrificing performance due to abstraction.

## Objects

C++ introduces object-oriented programming (OOP) features to C. It offers classes, which provide the four features commonly present in OOP (and some non-OOP) languages: abstraction, encapsulation, inheritance, and polymorphism. One distinguishing feature of C++ classes compared to classes in other programming languages is support for deterministic destructors, which in turn provide support for the Resource Acquisition is Initialization (RAII) concept.

## Encapsulation

Encapsulation is the hiding of information to ensure that data structures and operators are used as intended and to make the usage model more obvious to the developer. C++ provides the ability to define classes and functions as its primary encapsulation mechanisms. Within a class, members can be declared as either public, protected, or private to explicitly enforce encapsulation. A public member of the class is accessible to any function. A private member is accessible only to functions that are members of that class and to functions and classes explicitly granted access permission by the class ("friends"). A protected member is accessible to members of classes that inherit from the class in addition to the class itself and any friends.

The object-oriented principle ensures the encapsulation of all and only the functions that access the internal representation of a type. C++ supports this principle via member functions and friend functions, but it does not enforce it. Programmers can declare parts or all of the representation of a type to be public, and they are allowed to make public entities not part of the representation of a type. Therefore, C++ supports not just object-oriented programming, but other decomposition paradigms such as modular programming.

It is generally considered good practice to make all data private or protected, and to make public only those functions that are part of a minimal interface for users of the class. This can hide the details of data implementation, allowing the designer to later fundamentally change the implementation without changing the interface in any way.

## Inheritance

Inheritance allows one data type to acquire properties of other data types. Inheritance from a base class may be declared as public, protected, or private. This access specifier determines whether unrelated and derived classes can access the inherited public and protected members of the base class. Only public inheritance corresponds to what is usually meant by "inheritance". The other two forms are much less frequently used. If the access specifier is omitted, a "class" inherits privately, while a "struct" inherits publicly. Base classes may be declared as virtual; this is called virtual inheritance. Virtual inheritance ensures that only one instance of a base class exists in the inheritance graph, avoiding some of the ambiguity problems of multiple inheritance.

Multiple inheritance is a C++ feature allowing a class to be derived from more than one base class; this allows for more elaborate inheritance relationships. For example, a "Flying Cat" class can inherit from both "Cat" and "Flying Mammal". Some other languages, such as C# or Java, accomplish something similar (although more limited) by allowing inheritance of multiple interfaces while restricting the number of base classes to one (interfaces, unlike classes, provide only declarations of member functions, no implementation or member data). An interface as in C# and Java can be defined in C++ as a class containing only pure virtual functions, often known as an abstract base class or "ABC". The member functions of such an abstract base class are normally explicitly defined in the derived class, not inherited implicitly. C++ virtual inheritance exhibits an ambiguity resolution feature called dominance.

Operators and operator overloading

Operators that cannot be overloaded Operator 	Symbol

Scope resolution operator 	::

Conditional operator 	?:

dot operator 	.

Member selection operator 	.*

"sizeof" operator 	sizeof

"typeid" operator 	typeid

C++ provides more than 35 operators, covering basic arithmetic, bit manipulation, indirection, comparisons, logical operations and others. Almost all operators can be overloaded for user-defined types, with a few notable exceptions such as member access (. and .*) as well as the conditional operator. The rich set of overloadable operators is central to making user-defined types in C++ seem like built-in types.

Overloadable operators are also an essential part of many advanced C++ programming techniques, such as smart pointers. Overloading an operator does not change the precedence of calculations involving the operator, nor does it change the number of operands that the operator uses (any operand may however be ignored by the operator, though it will be evaluated prior to execution). Overloaded "&&" and "||" operators lose their short-circuit evaluation property.

## Polymorphism

Polymorphism enables one common interface for many implementations, and for objects to act differently under different circumstances.

C++ supports several kinds of static (resolved at compile-time) and dynamic (resolved at run-time) polymorphisms, supported by the language features described above. Compile-time polymorphism does not allow for certain run-time decisions, while runtime polymorphism typically incurs a performance penalty.

### Static polymorphism

Function overloading allows programs to declare multiple functions having the same name but with different arguments (i.e. ad hoc polymorphism). The functions are distinguished by the number or types of their formal parameters. Thus, the same function name can refer to different functions depending on the context in which it is used. The type returned by the function is not used to distinguish overloaded functions and differing return types would result in a compile-time error message.

When declaring a function, a programmer can specify for one or more parameters a default value. Doing so allows the parameters with defaults to optionally be omitted when the function is called, in which case the default arguments will be used. When a function is called with fewer arguments than there are declared parameters, explicit arguments are matched to parameters in left-to-right order, with any unmatched parameters at the end of the parameter list being assigned their default arguments. In many cases, specifying default arguments in a single function declaration is preferable to providing overloaded function definitions with different numbers of parameters.

Templates in C++ provide a sophisticated mechanism for writing generic, polymorphic code (i.e. parametric polymorphism). In particular, through the curiously recurring template pattern, it's possible to implement a form of static polymorphism that closely mimics the syntax for overriding virtual functions. Because C++ templates are type-aware and Turing-complete, they can also be used to let the compiler resolve recursive conditionals and generate substantial programs through template metaprogramming. Contrary to some opinion, template code will not generate a bulk code after compilation with the proper compiler settings.

### Dynamic polymorphism

#### Inheritance

Variable pointers and references to a base class type in C++ can also refer to objects of any derived classes of that type. This allows arrays and other kinds of containers to hold pointers to objects of differing types (references cannot be directly held in containers). This enables dynamic (run-time) polymorphism, where the referred objects can behave differently, depending on their (actual, derived) types.

C++ also provides the dynamic_cast operator, which allows code to safely attempt conversion of an object, via a base reference/pointer, to a more derived type: downcasting. The attempt is necessary as often one does not know which derived type is referenced. (Upcasting, conversion to a more general type, can always be checked/performed at compile-time via static_cast, as ancestral classes are specified in the derived class's interface, visible to all callers.) dynamic_cast relies on run-time type information (RTTI), metadata in the program that enables differentiating types and their relationships. If a dynamic_cast to a pointer fails, the result is the nullptr constant, whereas if the destination is a reference (which cannot be null), the cast throws an exception. Objects known to be of a certain derived type can be cast to that with static_cast, bypassing RTTI and the safe runtime type-checking of dynamic_cast, so this should be used only if the programmer is very confident the cast is, and will always be, valid.

#### Virtual member functions

Ordinarily, when a function in a derived class overrides a function in a base class, the function to call is determined by the type of the object. A given function is overridden when there exists no difference in the number or type of parameters between two or more definitions of that function. Hence, at compile time, it may not be possible to determine the type of the object and therefore the correct function to call, given only a base class pointer; the decision is therefore put off until runtime. This is called dynamic dispatch. Virtual member functions or methods allow the most specific implementation of the function to be called, according to the actual run-time type of the object. In C++ implementations, this is commonly done using virtual function tables. If the object type is known, this may be bypassed by prepending a fully qualified class name before the function call, but in general calls to virtual functions are resolved at run time.

In addition to standard member functions, operator overloads and destructors can be virtual. As a rule of thumb, if any function in the class is virtual, the destructor should be as well. As the type of an object at its creation is known at compile time, constructors, and by extension copy constructors, cannot be virtual. Nonetheless a situation may arise where a copy of an object needs to be created when a pointer to a derived object is passed as a pointer to a base object. In such a case, a common solution is to create a clone() (or similar) virtual function that creates and returns a copy of the derived class when called.

A member function can also be made "pure virtual" by appending it with = 0 after the closing parenthesis and before the semicolon. A class containing a pure virtual function is called an abstract class. Objects cannot be created from an abstract class; they can only be derived from. Any derived class inherits the virtual function as pure and must provide a non-pure definition of it (and all other pure virtual functions) before objects of the derived class can be created. A program that attempts to create an object of a class with a pure virtual member function or inherited pure virtual member function is ill-formed.

### Lambda expressions

C++ provides support for anonymous functions, also known as lambda expressions, with the following form:

```cpp
[capture](parameters) -> return_type { function_body }
```

Since C++20, you can write template parameters without the keyword template:

```cpp
[capture]<template_parameters>(parameters) -> return_type { function_body }
```

If the lambda takes no parameters, the () can be omitted, that is,

```cpp
[capture] -> return_type { function_body }
```

Also, the return type of a lambda expression can be automatically inferred, if possible, e.g.:

```cpp
[](int x, int y) { return x + y; } // inferred
[](int x, int y) -> int { return x + y; } // explicit
```

The [capture] list supports the definition of closures. Such lambda expressions are defined in the standard as syntactic sugar for an unnamed function object.

## Exception handling

Exception handling is used to communicate the existence of a runtime problem or error from where it was detected to where the issue can be handled. It permits this to be done in a uniform manner and separately from the main code, while detecting all errors. Should an error occur, an exception is thrown (raised), which is then caught by the nearest suitable exception handler. The exception causes the current scope to be exited, and also each outer scope (propagation) until a suitable handler is found, calling in turn the destructors of any objects in these exited scopes. At the same time, an exception is presented as an object carrying the data about the detected problem.

Some C++ style guides, such as Google's, LLVM's, and Qt's] forbid the usage of exceptions.

The exception-causing code is placed inside a try block. The exceptions are handled in separate catch blocks (the handlers); each try block can have multiple exception handlers, as it is visible in the example below.

```cpp
#include <iostream>
#include <vector>
#include <stdexcept>

int main() {
    try {
        std::vector<int> vec{3, 4, 3, 1};
        int i{vec.at(4)}; // Throws an exception, std::out_of_range (indexing for vec is from 0-3 not 1-4)
    }
    // An exception handler, catches std::out_of_range, which is thrown by vec.at(4)
    catch (std::out_of_range &e) {
        std::cerr << "Accessing a non-existent element: " << e.what() << '\n';
    }
    // To catch any other standard library exceptions (they derive from std::exception)
    catch (std::exception &e) {
        std::cerr << "Exception thrown: " << e.what() << '\n';
    }
    // Catch any unrecognised exceptions (i.e. those which don't derive from std::exception)
    catch (...) {
        std::cerr << "Some fatal error\n";
    }
}
```

It is also possible to raise exceptions purposefully, using the throw keyword; these exceptions are handled in the usual way. In some cases, exceptions cannot be used due to technical reasons. One such example is a critical component of an embedded system, where every operation must be guaranteed to complete within a specified amount of time. This cannot be determined with exceptions as no tools exist to determine the maximum time required for an exception to be handled.

Unlike signal handling, in which the handling function is called from the point of failure, exception handling exits the current scope before the catch block is entered, which may be located in the current function or any of the previous function calls currently on the stack.

## Standard library

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/ANSI_ISO_C%2B%2B_WP.jpg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/C%2B%2B/ANSI_ISO_C%2B%2B_WP.jpg)

The draft "Working Paper" standard that became approved as C++98; half of its size was devoted to the C++ Standard Library.

The C++ standard consists of two parts: the core language and the standard library. C++ programmers expect the latter on every major implementation of C++; it includes aggregate types (vectors, lists, maps, sets, queues, stacks, arrays, tuples), algorithms (find, for_each, binary_search, random_shuffle, etc.), input/output facilities (iostream, for reading from and writing to the console and files), filesystem library, localisation support, smart pointers for automatic memory management, regular expression support, multi-threading library, atomics support (allowing a variable to be read or written to by at most one thread at a time without any external synchronisation), time utilities (measurement, getting current time, etc.), a system for converting error reporting that doesn't use C++ exceptions into C++ exceptions, a random number generator and a slightly modified version of the C standard library (to make it comply with the C++ type system).

A large part of the C++ library is based on the Standard Template Library (STL). Useful tools provided by the STL include containers as the collections of objects (such as vectors and lists), iterators that provide array-like access to containers, and algorithms that perform operations such as searching and sorting.

Furthermore, (multi)maps (associative arrays) and (multi)sets are provided, all of which export compatible interfaces. Therefore, using templates it is possible to write generic algorithms that work with any container or on any sequence defined by iterators. As in C, the features of the library are accessed by using the #include directive to include a standard header. The C++ Standard Library provides 105 standard headers, of which 27 are deprecated.

The standard incorporates the STL that was originally designed by Alexander Stepanov, who experimented with generic algorithms and containers for many years. When he started with C++, he finally found a language where it was possible to create generic algorithms (e.g., STL sort) that perform even better than, for example, the C standard library qsort, thanks to C++ features like using inlining and compile-time binding instead of function pointers. The standard does not refer to it as "STL", as it is merely a part of the standard library, but the term is still widely used to distinguish it from the rest of the standard library (input/output streams, internationalization, diagnostics, the C library subset, etc.).

Most C++ compilers, and all major ones, provide a standards-conforming implementation of the C++ standard library.

## C++ Core Guidelines

The C++ Core Guidelines are an initiative led by Bjarne Stroustrup, the inventor of C++, and Herb Sutter, the convener and chair of the C++ ISO Working Group, to help programmers write 'Modern C++' by using best practices for the language standards C++14 and newer, and to help developers of compilers and static checking tools to create rules for catching bad programming practices.

The main aim is to efficiently and consistently write type and resource safe C++.

The Core Guidelines were announced in the opening keynote at CPPCon 2015.

The Guidelines are accompanied by the Guideline Support Library (GSL), a header only library of types and functions to implement the Core Guidelines and static checker tools for enforcing Guideline rules.

### Compatibility

To give compiler vendors greater freedom, the C++ standards committee decided not to dictate the implementation of name mangling, exception handling, and other implementation-specific features. The downside of this decision is that object code produced by different compilers is expected to be incompatible. There were, however, attempts to standardize compilers for particular machines or operating systems (for example C++ ABI), though they seem to be largely abandoned now.

#### With C

C++ is often considered to be a superset of C but this is not strictly true. Most C code can easily be made to compile correctly in C++ but there are a few differences that cause some valid C code to be invalid or behave differently in C++. For example, C allows implicit conversion from void* to other pointer types but C++ does not (for type safety reasons). Also, C++ defines many new keywords, such as new and class, which may be used as identifiers (for example, variable names) in a C program.

Some incompatibilities have been removed by the 1999 revision of the C standard (C99), which now supports C++ features such as line comments (//) and declarations mixed with code. On the other hand, C99 introduced a number of new features that C++ did not support that were incompatible or redundant in C++, such as variable-length arrays, native complex-number types (however, the std::complex class in the C++ standard library provides similar functionality, although not code-compatible), designated initializers, compound literals, and the restrict keyword. Some of the C99-introduced features were included in the subsequent version of the C++ standard, C++11 (out of those which were not redundant). However, the C++11 standard introduces new incompatibilities, such as disallowing assignment of a string literal to a character pointer, which remains valid C.

To intermix C and C++ code, any function declaration or definition that is to be called from/used both in C and C++ must be declared with C linkage by placing it within an extern "C" {/*...*/} block. Such a function may not rely on features depending on name mangling (i.e., function overloading).

### Criticism
Main article: Criticism of C++

Despite its widespread adoption, some notable programmers have criticized the C++ language, including Linus Torvalds, Richard Stallman, Joshua Bloch, Ken Thompson and Donald Knuth.

One of the most often criticised points of C++ is its perceived complexity as a language, with the criticism that a large number of non-orthogonal features in practice necessitates restricting code to subset of C++, thus eschewing the readability benefits of common style and idioms. As expressed by Joshua Bloch:

I think C++ was pushed well beyond its complexity threshold, and yet there are a lot of people programming it. But what you do is you force people to subset it. So almost every shop that I know of that uses C++ says, "Yes, we're using C++ but we're not doing multiple-implementation inheritance and we're not using operator overloading.” There are just a bunch of features that you're not going to use because the complexity of the resulting code is too high. And I don't think it's good when you have to start doing that. You lose this programmer portability where everyone can read everyone else's code, which I think is such a good thing. 

Donald Knuth (1993, commenting on pre-standardized C++), who said of Edsger Dijkstra that "to think of programming in C++" "would make him physically ill":

The problem that I have with them today is that... C++ is too complicated. At the moment, it's impossible for me to write portable code that I believe would work on lots of different systems, unless I avoid all exotic features. Whenever the C++ language designers had two competing ideas as to how they should solve some problem, they said "OK, we'll do them both". So the language is too baroque for my taste. 

Ken Thompson, who was a colleague of Stroustrup at Bell Labs, gives his assessment:

It certainly has its good points. But by and large I think it's a bad language. It does a lot of things half well and it's just a garbage heap of ideas that are mutually exclusive. Everybody I know, whether it's personal or corporate, selects a subset and these subsets are different. So it's not a good language to transport an algorithm—to say, "I wrote it; here, take it." It's way too big, way too complex. And it's obviously built by a committee. Stroustrup campaigned for years and years and years, way beyond any sort of technical contributions he made to the language, to get it adopted and used. And he sort of ran all the standards committees with a whip and a chair. And he said "no" to no one. He put every feature in that language that ever existed. It wasn't cleanly designed—it was just the union of everything that came along. And I think it suffered drastically from that. 

However Brian Kernighan, also a colleague at Bell Labs, disputes this assessment:

C++ has been enormously influential. ... Lots of people say C++ is too big and too complicated etc. etc. but in fact it is a very powerful language and pretty much everything that is in there is there for a really sound reason: it is not somebody doing random invention, it is actually people trying to solve real world problems. Now a lot of the programs that we take for granted today, that we just use, are C++ programs. 

Stroustrup himself comments that C++ semantics are much cleaner than its syntax: "within C++, there is a much smaller and cleaner language struggling to get out".

Other complaints may include a lack of reflection or garbage collection, long compilation times, perceived feature creep, and verbose error messages, particularly from template metaprogramming.

***

## Sources

[Wikipedia - C++](https://en.wikipedia.org/wiki/C%2B%2B)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `Sunday, 2021 September 19th at 6:09 pm)`

**File format:** `Markdown document (*.md *.mkd *.markdown)`

**Article language:** `English (US) / Markdown`

**Line count (including blank lines and compiler line):** `422`

**Article version:** `1 (Sunday, 2021 September 26th at 6:09 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
