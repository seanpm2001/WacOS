  
***

# Python (Programming language)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Python_logo_and_wordmark.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Python_logo_and_wordmark.svg)

( **Influenced by:** `ABC`, `Ada`, `ALGOL 68`, `APL`, `C`, `C++`, `CLU`, `Dylan`, `Haskell`, `Icon`, `Java`, `Lisp`, `Modula-3`, `Perl`, `Standard ML` | **Influenced:**
`Apache Groovy`, `Boo`, `Cobra`, `CoffeeScript`, `D`, `F#`, `Genie`, `Go`, `JavaScript`, `Julia`, `Nim`, `Ring`, `Ruby`, `Swift` | **Latest stableversion:** `3.9.7 (2021 August 30th)` | **Initial release:** `Python 0.9.0 (1991)` )

Python is an interpreted high-level general-purpose programming language. Its design philosophy emphasizes code readability with its use of significant indentation. Its language constructs as well as its object-oriented approach aim to help programmers write clear, logical code for small and large-scale projects.

Python is dynamically-typed and garbage-collected. It supports multiple programming paradigms, including structured (particularly, procedural), object-oriented and functional programming. It is often described as a "batteries included" language due to its comprehensive standard library.

Guido van Rossum began working on Python in the late 1980s, as a successor to the ABC programming language, and first released it in 1991 as Python 0.9.0. Python 2.0 was released in 2000 and introduced new features, such as list comprehensions and a garbage collection system using reference counting. Python 3.0 was released in 2008 and was a major revision of the language that is not completely backward-compatible. Python 2 was discontinued with version 2.7.18 in 2020.

Python consistently ranks as one of the most popular programming languages.

## History

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Guido_van_Rossum_OSCON_2006_cropped.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Guido_van_Rossum_OSCON_2006_cropped.png)

The designer of Python, Guido van Rossum, at OSCON 2006

Python was conceived in the late 1980s by Guido van Rossum at Centrum Wiskunde & Informatica (CWI) in the Netherlands as a successor to ABC programming language, which was inspired by SETL, capable of exception handling and interfacing with the Amoeba operating system. Its implementation began in December 1989. Van Rossum shouldered sole responsibility for the project, as the lead developer, until 12 July 2018, when he announced his "permanent vacation" from his responsibilities as Python's Benevolent Dictator For Life, a title the Python community bestowed upon him to reflect his long-term commitment as the project's chief decision-maker. In January 2019, active Python core developers elected a 5-member "Steering Council" to lead the project. As of 2021, the current members of this council are Barry Warsaw, Brett Cannon, Carol Willing, Thomas Wouters, and Pablo Galindo Salgado.

Python 2.0 was released on 16 October 2000, with many major new features, including a cycle-detecting garbage collector and support for Unicode.

Python 3.0 was released on 3 December 2008. It was a major revision of the language that is not completely backward-compatible. Many of its major features were backported to Python 2.6. and 2.7.x version series. Releases of Python 3 include the 2to3 utility, which automates (at least partially) the translation of Python 2 code to Python 3.

Python 2.7's end-of-life date was initially set at 2015 then postponed to 2020 out of concern that a large body of existing code could not easily be forward-ported to Python 3. No more security patches or other improvements will be released for it. With Python 2's end-of-life, only Python 3.6.x and later are supported.

Python 3.9.2 and 3.8.8 were expedited as all versions of Python (including 2.7) had security issues, leading to possible remote code execution and web cache poisoning.

## Design philosophy and features

Python is a multi-paradigm programming language. Object-oriented programming and structured programming are fully supported, and many of its features support functional programming and aspect-oriented programming (including by metaprogramming and metaobjects (magic methods)). Many other paradigms are supported via extensions, including design by contract and logic programming.

Python uses dynamic typing and a combination of reference counting and a cycle-detecting garbage collector for memory management. It also features dynamic name resolution (late binding), which binds method and variable names during program execution.

Python's design offers some support for functional programming in the Lisp tradition. It has filter,mapandreduce functions; list comprehensions, dictionaries, sets, and generator expressions. The standard library has two modules (itertools and functools) that implement functional tools borrowed from Haskell and Standard ML.

The language's core philosophy is summarized in the document The Zen of Python (PEP 20), which includes aphorisms such as:

Beautiful is better than ugly.

Explicit is better than implicit.

Simple is better than complex.

Complex is better than complicated.

Readability counts.

Rather than having all of its functionality built into its core, Python was designed to be highly extensible (with modules). This compact modularity has made it particularly popular as a means of adding programmable interfaces to existing applications. Van Rossum's vision of a small core language with a large standard library and easily extensible interpreter stemmed from his frustrations with ABC, which espoused the opposite approach.

Python strives for a simpler, less-cluttered syntax and grammar while giving developers a choice in their coding methodology. In contrast to Perl's "there is more than one way to do it" motto, Python embraces a "there should be one— and preferably only one —obvious way to do it" design philosophy. Alex Martelli, a Fellow at the Python Software Foundation and Python book author, writes that "To describe something as 'clever' is not considered a compliment in the Python culture."

Python's developers strive to avoid premature optimization, and reject patches to non-critical parts of the CPython reference implementation that would offer marginal increases in speed at the cost of clarity. When speed is important, a Python programmer can move time-critical functions to extension modules written in languages such as C, or use PyPy, a just-in-time compiler. Cython is also available, which translates a Python script into C and makes direct C-level API calls into the Python interpreter.

Python's developers aim to keep the language fun to use. This is reflected in its name—a tribute to the British comedy group Monty Python—and in occasionally playful approaches to tutorials and reference materials, such as examples that refer to spam and eggs (a reference to a Monty Python sketch) instead of the standard foo and bar.

A common neologism in the Python community is pythonic, which can have a wide range of meanings related to program style. To say that code is pythonic is to say that it uses Python idioms well, that it is natural or shows fluency in the language, that it conforms with Python's minimalist philosophy and emphasis on readability. In contrast, code that is difficult to understand or reads like a rough transcription from another programming language is called unpythonic.

Users and admirers of Python, especially those considered knowledgeable or experienced, are often referred to as Pythonistas.

## Syntax and semantics

The syntax of the Python programming language is the set of rules that defines how a Python program will be written and interpreted (by both the runtime system and by human readers). The Python language has many similarities to Perl, C, and Java. However, there are some definite differences between the languages.

## Design philosophy (syntax and semantics)

Python was designed to be a highly readable language. It has a relatively uncluttered visual layout and uses English keywords frequently where other languages use punctuation. Python aims to be simple and consistent in the design of its syntax, encapsulated in the mantra "There should be one— and preferably only one —obvious way to do it", from the Zen of Python.

This mantra is deliberately opposed to the Perl and Ruby mantra, "there's more than one way to do it".
Keywords

Python has 35 keywords or reserved words; they cannot be used as identifiers

`and`

`as`

`assert`

`async`

`await`

`break`

`class`

`continue`

`def`

`del`

`elif`

`else`

`except`

`False`

`finally`

`for`

`from`

`global`

`if`

`import`

`in`

`is`

`lambda`

`None`

`nonlocal`

`not`

`or`

`pass`

`raise`

`return`

`True`

`try`

`while`

`with`

`yield`

## Notes (syntax and semantics)

async and await were introduced in Python 3.5.

True and False became keywords in Python 3.0. Previously they were global variables.

nonlocal was introduced in Python 3.0.

## Indentation (syntax and semantics)

Python uses whitespace to delimit control flow blocks (following the off-side rule). Python borrows this feature from its predecessor ABC: instead of punctuation or keywords, it uses indentation to indicate the run of a block.

In so-called "free-format" languages—that use the block structure derived from ALGOL—blocks of code are set off with braces ({ }) or keywords. In most coding conventions for these languages, programmers conventionally indent the code within a block, to visually set it apart from the surrounding code.

A recursive function named foo, which is passed a single parameter, x, and if the parameter is 0 will call a different function named bar and otherwise will call baz, passing x, and also call itself recursively, passing x-1 as the parameter, could be implemented like this in Python:

```python
def foo(x):
    if x == 0:
        bar()
    else:
        baz(x)
        foo(x - 1)
```

and could be written like this in C with K&R indent style:

```python
void foo(int x)
{
    if (x == 0) {
        bar();
    } else {
        baz(x);
        foo(x - 1);
    }
}
```

Python mandates a convention that programmers in ALGOL-style languages often follow.

Incorrectly indented code could be misread by a human reader differently than it would be interpreted by a compiler or interpreter. This example illustrates an error introduced by incorrect indentation:

```python
def foo(x):
    if x == 0:
        bar()
    else:
        baz(x)
    foo(x - 1)
```

Here, in contrast to the above foo example, the function call foo(x - 1) on the last line is erroneously indented to be outside the if/else block. This would cause it to always be executed, even when x is 0, resulting in an endless recursion.

While both space and tab characters are accepted as forms of indentation and any multiple of spaces can be used, spaces are recommended and 4 spaces (as in this article) are recommended and are by far the most commonly used. Mixing spaces and tabs on consecutive lines in the same source code file is not allowed starting with Python 3 because that can create bugs which are difficult to see, since many tools do not visually distinguish spaces and tabs.

## Data structures (syntax and semantics)

Since Python is a dynamically typed language, Python values, not variables, carry type information. This has implications for many aspects of the way the language functions.

All variables in Python hold references to objects, and these references are passed to functions; a function cannot change the value of variable references in its calling function (but see below for exceptions). Some people (including Guido van Rossum himself) have called this parameter-passing scheme "call by object reference". An object reference means a name, and the passed reference is an "alias", i.e. a copy of the reference to the same object, just as in C/C++. The object's value may be changed in the called function with the "alias", for example:

```python
>>> alist = ['a', 'b', 'c']
>>> def my_func(al):
...     al.append('x')
...     print(al)
...
>>> my_func(alist)
['a', 'b', 'c', 'x']
>>> alist
['a', 'b', 'c', 'x']
```

Function my_func changed the value of alist with the formal argument al, which is an alias of alist. However, any attempt to operate on the alias itself will have no effect on the original object. In Python, non-innermost-local and not-declared-global accessible names are all aliases.

Among dynamically typed languages, Python is moderately type-checked. Implicit conversion is defined for numeric types (as well as booleans), so one may validly multiply a complex number by an integer (for instance) without explicit casting. However, there is no implicit conversion between, for example, numbers and strings; a string is an invalid argument to a mathematical function expecting a number.

## Base types (syntax and semantics)

Python has a broad range of basic data types. Alongside conventional integer and floating-point arithmetic, it transparently supports arbitrary-precision arithmetic, complex numbers, and decimal numbers.

Python supports a wide variety of string operations. Strings in Python are immutable, so a string operation such as a substitution of characters, that in other programming languages might alter the string in place, returns a new string in Python. Performance considerations sometimes push for using special techniques in programs that modify strings intensively, such as joining character arrays into strings only as needed.
Collection types

One of the very useful aspects of Python is the concept of collection (or container) types. In general a collection is an object that contains other objects in a way that is easily referenced or indexed. Collections come in two basic forms: sequences and mappings.

The ordered sequential types are lists (dynamic arrays), tuples, and strings. All sequences are indexed positionally (0 through length − 1) and all but strings can contain any type of object, including multiple types in the same sequence. Both strings and tuples are immutable, making them perfect candidates for dictionary keys (see below). Lists, on the other hand, are mutable; elements can be inserted, deleted, modified, appended, or sorted in-place.

Mappings, on the other hand, are (often unordered) types implemented in the form of dictionaries which "map" a set of immutable keys to corresponding elements (much like a mathematical function). For example, one could define a dictionary having a string "toast" mapped to the integer 42 or vice versa. The keys in a dictionary must be of an immutable Python type, such as an integer or a string, because under the hood they are implemented via a hash function. This makes for much faster lookup times, but requires keys not change.

Dictionaries are central to the internals of Python as they reside at the core of all objects and classes: the mappings between variable names (strings) and the values which the names reference are stored as dictionaries (see Object system). Since these dictionaries are directly accessible (via an object's __dict__ attribute), metaprogramming is a straightforward and natural process in Python.

A set collection type is an unindexed, unordered collection that contains no duplicates, and implements set theoretic operations such as union, intersection, difference, symmetric difference, and subset testing. There are two types of sets: set and frozenset, the only difference being that set is mutable and frozenset is immutable. Elements in a set must be hashable. Thus, for example, a frozenset can be an element of a regular set whereas the opposite is not true.

Python also provides extensive collection manipulating abilities such as built in containment checking and a generic iteration protocol.

## Object system (syntax and semantics)

In Python, everything is an object, even classes. Classes, as objects, have a class, which is known as their metaclass. Python also supports multiple inheritance and mixins.

The language supports extensive introspection of types and classes. Types can be read and compared—types are instances of type. The attributes of an object can be extracted as a dictionary.

Operators can be overloaded in Python by defining special member functions - for instance, defining a method named __add__ on a class permits one to use the + operator on objects of that class.

## Literals (syntax and semantics)

### Strings (syntax and semantics)

Python has various kinds of string literals.

#### Normal string literals (syntax and semantics)

Either single or double quotes can be used to quote strings. Unlike in Unix shell languages, Perl or Perl-influenced languages such as Ruby or Groovy, single quotes and double quotes function identically, i.e. there is no string interpolation of $foo expressions. However, interpolation can be done in various ways: with "f-strings" (since Python 3.6), using the format method or the old % string-format operator.

For instance, the Perl statement:

```perl
print "I just printed $num pages to the printer $printer\n"
```

is equivalent to any of these Python statements:

```python
print(f"I just printed {num} pages to the printer {printer}")
```

```python
print("I just printed {} pages to the printer {}".format(num, printer))
print("I just printed {0} pages to the printer {1}".format(num, printer))
print("I just printed {num} pages to the printer {printer}".format(num=num, printer=printer))
```

```python
print("I just printed %s pages to the printer %s" % (num, printer))
print("I just printed %(num)s pages to the printer %(printer)s" % {"num": num, "printer": printer})
```

#### Multi-line string literals (syntax and semantics)

There are also multi-line strings, which begin and end with a series of three single or double quotes and function like here documents in Perl and Ruby.

A simple example with variable interpolation (using the format method) is:

```python
print("""Dear {recipient},

I wish you to leave Sunnydale and never return.

Not Quite Love,
{sender}
""".format(sender="Buffy the Vampire Slayer", recipient="Spike"))
```

#### Raw strings (syntax and semantics)

Finally, all of the previously mentioned string types come in "raw" varieties (denoted by placing a literal r before the opening quote), which do no backslash-interpolation and hence are very useful for regular expressions; compare "@-quoting" in C#. Raw strings were originally included specifically for regular expressions. Due to limitations of the tokenizer, raw strings may not have a trailing backslash. Creating a raw string holding a Windows path ending with a backslash requires some variety of workaround (commonly, using forward slashes instead of backslashes, since Windows accepts both).

Examples include:

```python
>>> # A Windows path, even raw strings cannot end in a backslash
>>> r"C:\Foo\Bar\Baz\"
  File "<stdin>", line 1
    r"C:\Foo\Bar\Baz\"
                     ^
SyntaxError: EOL while scanning string literal
```

```python
>>> dos_path = r"C:\Foo\Bar\Baz\ " # avoids the error by adding
>>> dos_path.rstrip()              # and removing trailing space
'C:\\Foo\\Bar\\Baz\\'
```

```python
>>> quoted_dos_path = r'"{}"'.format(dos_path)
>>> quoted_dos_path
'"C:\\Foo\\Bar\\Baz\\ "'
```

```python
>>> # A regular expression matching a quoted string with possible backslash quoting
>>> re.match(r'"(([^"\\]|\\.)*)"', quoted_dos_path).group(1).rstrip()
'C:\\Foo\\Bar\\Baz\\'
```

```python`
>>> code = 'foo(2, bar)'
>>> # Reverse the arguments in a two-arg function call
>>> re.sub(r'\(([^,]*?),([^ ,]*?)\)', r'(\2, \1)', code)
'foo(2, bar)'
>>> # Note that this won't work if either argument has parens or commas in it.
```

### Concatenation of adjacent string literals (syntax and semantics)

String literals (using possibly different quote conventions) appearing contiguously and only separated by whitespace (including new lines), are allowed and are aggregated into a single longer string.Thus

```python
title = "One Good Turn: " \
        'A Natural History of the Screwdriver and the Screw'
```

is equivalent to

```python
title = "One Good Turn: A Natural History of the Screwdriver and the Screw"
```

## Unicode (syntax and semantics)

Since Python 3.0, the default character set is UTF-8 both for source code and the interpreter. In UTF-8, unicode strings are handled like traditional byte strings. This example will work:

```python
s = "Γειά" # Hello in Greek
print(s)
```

## Numbers (syntax and semantics)

Numeric literals in Python are of the normal sort, e.g. 0, -1, 3.4, 3.5e-8.

Python has arbitrary-length integers and automatically increases their storage size as necessary. Prior to Python 3, there were two kinds of integral numbers: traditional fixed size integers and "long" integers of arbitrary size. The conversion to "long" integers was performed automatically when required, and thus the programmer usually didn't have to be aware of the two integral types. In newer language versions the distinction is completely gone and all integers behave like arbitrary-length integers.

Python supports normal floating point numbers, which are created when a dot is used in a literal (e.g. 1.1), when an integer and a floating point number are used in an expression, or as a result of some mathematical operations ("true division" via the / operator, or exponentiation with a negative exponent).

Python also supports complex numbers natively. Complex numbers are indicated with the J or j suffix, e.g. 3 + 4j.
Lists, tuples, sets, dictionaries

Python has syntactic support for the creation of container types.

Lists (class list) are mutable sequences of items of arbitrary types, and can be created either with the special syntax

```python
a_list = [1, 2, 3, "a dog"]
```

or using normal object creation

```python
a_second_list = list()
a_second_list.append(4)
a_second_list.append(5)
```

Tuples (class tuple) are immutable sequences of items of arbitrary types. There is also a special syntax to create tuples

```python
a_tuple = 1, 2, 3, "four"
a_tuple = (1, 2, 3, "four")
```

Although tuples are created by separating items with commas, the whole construct is usually wrapped in parentheses to increase readability. An empty tuple is denoted by ().

Sets (class set) are mutable containers of hashable items of arbitrary types, with no duplicates. The items are not ordered, but sets support iteration over the items. The syntax for set creation uses curly brackets

```python
some_set = {0, (), False}
```

Python sets are very much like mathematical sets, and support operations like set intersection and union. Python also features a frozenset class for immutable sets, see Collection types.

Dictionaries (class dict) are mutable mappings tying keys and corresponding values. Python has special syntax to create dictionaries ({key: value})

```python
a_dictionary = {"key 1": "value 1", 2: 3, 4: []}
```

The dictionary syntax is similar to the set syntax, the difference is the presence of colons. The empty literal {} results in an empty dictionary rather than an empty set, which is instead created using the non-literal constructor: set().

## Operators (syntax and semantics)

### Arithmetic (syntax and semantics)

Python includes the +, -, *, / ("true division"), // (floor division), % (modulus), and ** (exponentiation) operators, with their usual mathematical precedence.

In Python 3, x / y performs "true division", meaning that it always returns a float, even if both x and y are integers that divide evenly.

```python
>>> 4 / 2
2.0
```

and // performs integer division or floor division, returning the floor of the quotient as an integer.

In Python 2 (and most other programming languages), unless explicitly requested, x / y performed integer division, returning a float only if either input was a float. However, because Python is a dynamically typed language, it was not always possible to tell which operation was being performed, which often led to subtle bugs, thus prompting the introduction of the // operator and the change in semantics of the / operator in Python 3.

### Comparison operators (syntax and semantics)

The comparison operators, i.e. ==, !=, <, >, <=, >=, is, is not, in and not in are used on all manner of values. Numbers, strings, sequences, and mappings can all be compared. In Python 3, disparate types (such as a str and an int) do not have a consistent relative ordering. While it was possible to compare whether some string was greater-than or less-than some integer in Python 2, this was considered a historical design quirk and was ultimately removed in Python 3.

Chained comparison expressions such as a < b < c have roughly the meaning that they have in mathematics, rather than the unusual meaning found in C and similar languages. The terms are evaluated and compared in order. The operation has short-circuit semantics, meaning that evaluation is guaranteed to stop as soon as a verdict is clear: if a < b is false, c is never evaluated as the expression cannot possibly be true anymore.

For expressions without side effects, a < b < c is equivalent to a < b and b < c. However, there is a substantial difference when the expressions have side effects. a < f(x) < b will evaluate f(x) exactly once, whereas a < f(x) and f(x) < b will evaluate it twice if the value of a is less than f(x) and once otherwise.

### Logical operators  (syntax and semantics)

In all versions of Python, boolean operators treat zero values or empty values such as "", 0, None, 0.0, [], and {} as false, while in general treating non-empty, non-zero values as true. The boolean values True and False were added to the language in Python 2.2.1 as constants (subclassed from 1 and 0) and were changed to be full blown keywords in Python 3. The binary comparison operators such as == and > return either True or False.

The boolean operators and and or use minimal evaluation. For example, y == 0 or x/y > 100 will never raise a divide-by-zero exception. These operators return the value of the last operand evaluated, rather than True or False. Thus the expression (4 and 5) evaluates to 5, and (4 or 5) evaluates to 4.

## Functional programming (syntax and semantics)

As mentioned above, another strength of Python is the availability of a functional programming style. As may be expected, this makes working with lists and other collections much more straightforward.

### Comprehensions (syntax and semantics)

One such construction is the list comprehension, which can be expressed with the following format:

```python
L = [mapping_expression for element in source_list if filter_expression]
```

Using list comprehension to calculate the first five powers of two:

```python
powers_of_two = [2**n for n in range(1, 6)]
```

The Quicksort algorithm can be expressed elegantly (albeit inefficiently) using list comprehensions:

```python
def qsort(L):
    if L == []:
        return []
    pivot = L[0]
    return (qsort([x for x in L[1:] if x < pivot]) +
            [pivot] +
            qsort([x for x in L[1:] if x >= pivot]))
```

Python 2.7+ also supports set comprehensions and dictionary comprehensions.

### First-class functions (syntax and semantics)

In Python, functions are first-class objects that can be created and passed around dynamically.

Python's limited support for anonymous functions is the lambda construct. An example is the anonymous function which squares its input, called with the argument of 5:

```python
f = lambda x: x**2
f(5)
```

Lambdas are limited to containing an expression rather than statements, although control flow can still be implemented less elegantly within lambda by using short-circuiting, and more idiomatically with conditional expressions.

### Closures (syntax and semantics)

Python has had support for lexical closures since version 2.2. Here's an example function that returns a function that approximates the derivative of the given function:

```python
def derivative(f, dx):
    """Return a function that approximates the derivative of f
    using an interval of dx, which should be appropriately small.
    """
    def function(x):
        return (f(x + dx) - f(x)) / dx
    return function
```

Python's syntax, though, sometimes leads programmers of other languages to think that closures are not supported. Variable scope in Python is implicitly determined by the scope in which one assigns a value to the variable, unless scope is explicitly declared with global or nonlocal.

Note that the closure's binding of a name to some value is not mutable from within the function. Given:

```python
>>> def foo(a, b):
...     print(f'a: {a}')
...     print(f'b: {b}')
...     def bar(c):
...         b = c
...         print(f'b*: {b}')
...     bar(a)
...     print(f'b: {b}')
... 
>>> foo(1, 2)
a: 1
b: 2
b*: 1
b: 2
```

and you can see that b, as visible from the closure's scope, retains the value it had; the changed binding of b inside the inner function did not propagate out. The way around this is to use a nonlocal b statement in bar. In Python 2 (which lacks nonlocal), the usual workaround is to use a mutable value and change that value, not the binding. E.g., a list with one element.

### Generators (syntax and semantics)

Introduced in Python 2.2 as an optional feature and finalized in version 2.3, generators are Python's mechanism for lazy evaluation of a function that would otherwise return a space-prohibitive or computationally intensive list.

This is an example to lazily generate the prime numbers:

```python
from itertools import count

def generate_primes(stop_at=None):
    primes = []
    for n in count(start=2):
        if stop_at is not None and n > stop_at:
            return # raises the StopIteration exception
        composite = False
        for p in primes:
            if not n % p:
                composite = True
                break
            elif p ** 2 > n:
                break
        if not composite:
            primes.append(n)
            yield n
```

When calling this function, the returned value can be iterated over much like a list:

```python
for i in generate_primes(100):  # iterate over the primes between 0 and 100
    print(i)

for i in generate_primes():  # iterate over ALL primes indefinitely
    print(i)
```

The definition of a generator appears identical to that of a function, except the keyword yield is used in place of return. However, a generator is an object with persistent state, which can repeatedly enter and leave the same scope. A generator call can then be used in place of a list, or other structure whose elements will be iterated over. Whenever the for loop in the example requires the next item, the generator is called, and yields the next item.

Generators don't have to be infinite like the prime-number example above. When a generator terminates, an internal exception is raised which indicates to any calling context that there are no more values. A for loop or other iteration will then terminate.

### Generator expressions (syntax and semantics)

Introduced in Python 2.4, generator expressions are the lazy evaluation equivalent of list comprehensions. Using the prime number generator provided in the above section, we might define a lazy, but not quite infinite collection.

```python
from itertools import islice

primes_under_million = (i for i in generate_primes() if i < 1000000)
two_thousandth_prime = islice(primes_under_million, 1999, 2000).next()
```

Most of the memory and time needed to generate this many primes will not be used until the needed element is actually accessed. Unfortunately, you cannot perform simple indexing and slicing of generators, but must use the itertools module or "roll your own" loops. In contrast, a list comprehension is functionally equivalent, but is greedy in performing all the work:

```python
primes_under_million = [i for i in generate_primes(2000000) if i < 1000000]
two_thousandth_prime = primes_under_million[1999]
```

The list comprehension will immediately create a large list (with 78498 items, in the example, but transiently creating a list of primes under two million), even if most elements are never accessed. The generator comprehension is more parsimonious.

### Dictionary and set comprehensions (syntax and semantics)

While lists and generators had comprehensions/expressions, in Python versions older than 2.7 the other Python built-in collection types (dicts and sets) had to be kludged in using lists or generators:

```python
>>> dict((n, n*n) for n in range(5))
{0: 0, 1: 1, 2: 4, 3: 9, 4: 16}
```

Python 2.7 and 3.0 unified all collection types by introducing dictionary and set comprehensions, similar to list comprehensions:

```python
>>> [n*n for n in range(5)]  # regular list comprehension
[0, 1, 4, 9, 16]
>>>
>>> {n*n for n in range(5)}  # set comprehension
{0, 1, 4, 9, 16}
>>>
>>> {n: n*n for n in range(5)}  # dict comprehension
{0: 0, 1: 1, 2: 4, 3: 9, 4: 16}
```

### Objects (syntax and semantics)

Python supports most object oriented programming (OOP) techniques. It allows polymorphism, not only within a class hierarchy but also by duck typing. Any object can be used for any type, and it will work so long as it has the proper methods and attributes. And everything in Python is an object, including classes, functions, numbers and modules. Python also has support for metaclasses, an advanced tool for enhancing classes' functionality. Naturally, inheritance, including multiple inheritance, is supported. Python has very limited support for private variables using name mangling which is rarely used in practice as information hiding is seen by some as unpythonic, in that it suggests that the class in question contains unaesthetic or ill-planned internals. The slogan "we're all responsible users here" is used to describe this attitude.

As is true for modules, classes in Python do not put an absolute barrier between definition and user, but rather rely on the politeness of the user not to "break into the definition."

— 9. Classes, The Python 2.6 Tutorial (2013)

OOP doctrines such as the use of accessor methods to read data members are not enforced in Python. Just as Python offers functional-programming constructs but does not attempt to demand referential transparency, it offers an object system but does not demand OOP behavior. Moreover, it is always possible to redefine the class using properties (see Properties) so that when a certain variable is set or retrieved in calling code, it really invokes a function call, so that spam.eggs = toast might really invoke spam.set_eggs(toast). This nullifies the practical advantage of accessor functions, and it remains OOP because the property eggs becomes a legitimate part of the object's interface: it need not reflect an implementation detail.

In version 2.2 of Python, "new-style" classes were introduced. With new-style classes, objects and types were unified, allowing the subclassing of types. Even entirely new types can be defined, complete with custom behavior for infix operators. This allows for many radical things to be done syntactically within Python. A new method resolution order for multiple inheritance was also adopted with Python 2.3. It is also possible to run custom code while accessing or setting attributes, though the details of those techniques have evolved between Python versions.
With statement

The with statement handles resources, and allows users to work with the Context Manager protocol. One function (__enter__()) is called when entering scope and another (__exit__()) when leaving. This prevents forgetting to free the resource and also handles more complicated situations such as freeing the resource when an exception occurs while it is in use. Context Managers are often used with files, database connections, test cases, etc.

### Properties (syntax and semantics)

Properties allow specially defined methods to be invoked on an object instance by using the same syntax as used for attribute access. An example of a class defining some properties is:

```python
class MyClass:
    def __init__(self):
        self._a = None

    @property
    def a(self):
        return self._a

    @a.setter  # makes the property writable
    def a(self, value):
        self._a = value
```

### Descriptors  (syntax and semantics)

A class that defines one or more of the three special methods __get__(self, instance, owner), __set__(self, instance, value), __delete__(self, instance) can be used as a descriptor. Creating an instance of a descriptor as a class member of a second class makes the instance a property of the second class.

### Class and static methods (syntax and semantics)

Python allows the creation of class methods and static methods via the use of the @classmethod and @staticmethod decorators. The first argument to a class method is the class object instead of the self-reference to the instance. A static method has no special first argument. Neither the instance, nor the class object is passed to a static method.

### Exceptions (syntax and semantics)

Python supports (and extensively uses) exception handling as a means of testing for error conditions and other "exceptional" events in a program. Indeed, it is even possible to trap the exception caused by a syntax error.

Python style calls for the use of exceptions whenever an error condition might arise. Rather than testing for access to a file or resource before actually using it, it is conventional in Python to just go ahead and try to use it, catching the exception if access is rejected.

Exceptions can also be used as a more general means of non-local transfer of control, even when an error is not at issue. For instance, the Mailman mailing list software, written in Python, uses exceptions to jump out of deeply nested message-handling logic when a decision has been made to reject a message or hold it for moderator approval.

Exceptions are often used as an alternative to the if-block, especially in threaded situations. A commonly invoked motto is EAFP, or "It is Easier to Ask for Forgiveness than Permission," which is attributed to Grace Hopper. The alternative, known as LBYL, or "Look Before You Leap", explicitly tests for pre-conditions.

In this first code sample, following the LBYL approach, there is an explicit check for the attribute before access:

```python
if hasattr(spam, 'eggs'):
    ham = spam.eggs
else:
    handle_missing_attr()
```

This second sample follows the EAFP paradigm:

```python
try:
    ham = spam.eggs
except AttributeError:
    handle_missing_attr()
```

These two code samples have the same effect, although there will be performance differences. When spam has the attribute eggs, the EAFP sample will run faster. When spam does not have the attribute eggs (the "exceptional" case), the EAFP sample will run slower. The Python profiler can be used in specific cases to determine performance characteristics. If exceptional cases are rare, then the EAFP version will have superior average performance than the alternative. In addition, it avoids the whole class of time-of-check-to-time-of-use (TOCTTOU) vulnerabilities, other race conditions, and is compatible with duck typing. A drawback of EAFP is that it can be used only with statements; an exception cannot be caught in a generator expression, list comprehension, or lambda function.

## Comments and docstrings (syntax and semantics)

Python has two ways to annotate Python code. One is by using comments to indicate what some part of the code does. Single-line comments begin with the hash character (#) and continue until the end of the line. Comments spanning more than one line are achieved by inserting a multi-line string (with """ or ''' as the delimiter on each end) that is not used in assignment or otherwise evaluated, but sits in between other statements.

Commenting a piece of code:

```python
import sys

def getline():
    return sys.stdin.readline()  # Get one line and return it

Commenting a piece of code with multiple lines:

def getline():
    return sys.stdin.readline()    """this function
                                      gets one line
                                      and returns it"""
```

Docstrings (documentation strings), that is, strings that are located alone without assignment as the first indented line within a module, class, method or function, automatically set their contents as an attribute named __doc__, which is intended to store a human-readable description of the object's purpose, behavior, and usage. The built-in help function generates its output based on __doc__ attributes. Such strings can be delimited with " or ' for single line strings, or may span multiple lines if delimited with either """ or ''' which is Python's notation for specifying multi-line strings. However, the style guide for the language specifies that triple double quotes (""") are preferred for both single and multi-line docstrings.

Single line docstring:

```python
def getline():
    """Get one line from stdin and return it."""
    return sys.stdin.readline()
```

Multi-line docstring:

```python
def getline():
    """Get one line
       from stdin
       and return it.
    """
    return sys.stdin.readline()
```

Docstrings can be as large as the programmer wants and contain line breaks. In contrast with comments, docstrings are themselves Python objects and are part of the interpreted code that Python runs. That means that a running program can retrieve its own docstrings and manipulate that information, but the normal usage is to give other programmers information about how to invoke the object being documented in the docstring.

There are tools available that can extract the docstrings from Python code and generate documentation. Docstring documentation can also be accessed from the interpreter with the help() function, or from the shell with the pydoc command pydoc.

The doctest standard module uses interactions copied from Python shell sessions into docstrings to create tests, whereas the docopt module uses them to define command-line options.

## Function annotations (syntax and semantics)

Function annotations (type hints) are defined in PEP 3107. They allow attaching data to the arguments and return of a function. The behaviour of annotations is not defined by the language, and is left to third party frameworks. For example, a library could be written to handle static typing:

```python
def haul(item: Haulable, *vargs: PackAnimal) -> Distance
```

## Decorators (syntax and semantics)

A decorator is any callable Python object that is used to modify a function, method or class definition. A decorator is passed the original object being defined and returns a modified object, which is then bound to the name in the definition. Python decorators were inspired in part by Java annotations, and have a similar syntax; the decorator syntax is pure syntactic sugar, using @ as the keyword:

```python
@viking_chorus
def menu_item():
    print("spam")
```

is equivalent to

```python
def menu_item():
    print("spam")
menu_item = viking_chorus(menu_item)
```

Decorators are a form of metaprogramming; they enhance the action of the function or method they decorate. For example, in the sample below, viking_chorus might cause menu_item to be run 8 times (see Spam sketch) for each time it is called:

```python
def viking_chorus(myfunc):
    def inner_func(*args, **kwargs):
        for i in range(8):
            myfunc(*args, **kwargs)
    return inner_func
```

Canonical uses of function decorators are for creating class methods or static methods, adding function attributes, tracing, setting pre- and postconditions, and synchronization, but can be used for far more, including tail recursion elimination, memoization and even improving the writing of other decorators.

Decorators can be chained by placing several on adjacent lines:

```python
@invincible
@favourite_colour("Blue")
def black_knight():
    pass
```

is equivalent to

```python
def black_knight():
    pass
black_knight = invincible(favourite_colour("Blue")(black_knight))
```

or, using intermediate variables

```python
def black_knight():
    pass
blue_decorator = favourite_colour("Blue")
decorated_by_blue = blue_decorator(black_knight)
black_knight = invincible(decorated_by_blue)
```

In the example above, the favourite_colour decorator factory takes an argument. Decorator factories must return a decorator, which is then called with the object to be decorated as its argument:

```python
def favourite_colour(colour):
    def decorator(func):
        def wrapper():
            print(colour)
            func()
        return wrapper
    return decorator
```

This would then decorate the black_knight function such that the colour, "Blue", would be printed prior to the black_knight function running. Closure ensures that the colour argument is accessible to the innermost wrapper function even when it is returned and goes out of scope, which is what allows decorators to work.

Despite the name, Python decorators are not an implementation of the decorator pattern. The decorator pattern is a design pattern used in statically typed object-oriented programming languages to allow functionality to be added to objects at run time; Python decorators add functionality to functions and methods at definition time, and thus are a higher-level construct than decorator-pattern classes. The decorator pattern itself is trivially implementable in Python, because the language is duck typed, and so is not usually considered as such.

## Easter eggs (syntax and semantics)

Users of curly bracket languages, such as C or Java, sometimes expect or wish Python to follow a block-delimiter convention. Brace-delimited block syntax has been repeatedly requested, and consistently rejected by core developers. The Python interpreter contains an easter egg that summarizes its developers' feelings on this issue. The code from __future__ import braces raises the exception SyntaxError: not a chance. The __future__ module is normally used to provide features from future versions of Python.

Another hidden message, the Zen of Python (a summary of Python design philosophy), is displayed when trying to import this.

The message Hello world! is printed when the import statement import __hello__ is used. In Python 2.7, instead of Hello world! it prints Hello world....

Importing the antigravity module opens a web browser to xkcd comic 353 that portrays a humorous fictional use for such a module, intended to demonstrate the ease with which Python modules enable additional functionality. In Python 3, this module also contains an implementation of the "geohash" algorithm, a reference to xkcd comic 426.

Python is meant to be an easily readable language. Its formatting is visually uncluttered, and it often uses English keywords where other languages use punctuation. Unlike many other languages, it does not use curly brackets to delimit blocks, and semicolons after statements are allowed but are rarely, if ever, used. It has fewer syntactic exceptions and special cases than C or Pascal.

## Indentation

Python uses whitespace indentation, rather than curly brackets or keywords, to delimit blocks. An increase in indentation comes after certain statements; a decrease in indentation signifies the end of the current block. Thus, the program's visual structure accurately represents the program's semantic structure. This feature is sometimes termed the off-side rule, which some other languages share, but in most languages indentation does not have any semantic meaning. The recommended indent size is four spaces.

## Statements and control flow

Python's statements include (among others):

The assignment statement, using a single equals sign =.

The if statement, which conditionally executes a block of code, along with else and elif (a contraction of else-if).

The for statement, which iterates over an iterable object, capturing each element to a local variable for use by the attached block.

The while statement, which executes a block of code as long as its condition is true.

The try statement, which allows exceptions raised in its attached code block to be caught and handled by except clauses; it also ensures that clean-up code in a finally block will always be run regardless of how the block exits.

The raise statement, used to raise a specified exception or re-raise a caught exception.

The class statement, which executes a block of code and attaches its local namespace to a class, for use in object-oriented programming.

The def statement, which defines a function or method.

The with statement, which encloses a code block within a context manager (for example, acquiring a lock before the block of code is run and releasing the lock afterwards, or opening a file and then closing it), allowing resource-acquisition-is-initialization (RAII)-like behavior and replaces a common try/finally idiom.

The break statement, exits from a loop.

The continue statement, skips this iteration and continues with the next item.

The del statement, removes a variable, which means the reference from the name to the value is deleted and trying to use that variable will cause an error. A deleted variable can be reassigned.

The pass statement, which serves as a NOP. It is syntactically needed to create an empty code block.

The assert statement, used during debugging to check for conditions that should apply.

The yield statement, which returns a value from a generator function and yield is also an operator. This form is used to implement coroutines.

The return statement, used to return a value from a function.

The import statement, which is used to import modules whose functions or variables can be used in the current program.

The assignment statement (=) operates by binding a name as a reference to a separate, dynamically-allocated object. Variables may be subsequently rebound at any time to any object. In Python, a variable name is a generic reference holder and does not have a fixed data type associated with it. However, at a given time, a variable will refer to some object, which will have a type. This is referred to as dynamic typing and is contrasted with statically-typed programming languages, where each variable may only contain values of a certain type.

Python does not support tail call optimization or first-class continuations, and, according to Guido van Rossum, it never will. However, better support for coroutine-like functionality is provided, by extending Python's generators. Before 2.5, generators were lazy iterators; information was passed unidirectionally out of the generator. From Python 2.5, it is possible to pass information back into a generator function, and from Python 3.3, the information can be passed through multiple stack levels.

## Expressions

Some Python expressions are similar to those found in languages such as C and Java, while some are not:

Addition, subtraction, and multiplication are the same, but the behavior of division differs. There are two types of divisions in Python. They are floor division (or integer division) // and floating-point/division. Python also uses the ** operator for exponentiation.

From Python 3.5, the new @ infix operator was introduced. It is intended to be used by libraries such as NumPy for matrix multiplication.

From Python 3.8, the syntax :=, called the 'walrus operator' was introduced. It assigns values to variables as part of a larger expression.

In Python, == compares by value, versus Java, which compares numerics by value and objects by reference. (Value comparisons in Java on objects can be performed with the equals() method.) Python's is operator may be used to compare object identities (comparison by reference). In Python, comparisons may be chained, for example a <= b <= c.

Python uses the words and, or, not for its boolean operators rather than the symbolic &&, ||, ! used in Java and C.

Python has a type of expression termed a list comprehension as well as a more general expression termed a generator expression.

Anonymous functions are implemented using lambda expressions; however, these are limited in that the body can only be one expression.

Conditional expressions in Python are written as x if c else y (different in order of operands from the c ? x : y operator common to many other languages).

Python makes a distinction between lists and tuples. Lists are written as [1, 2, 3], are mutable, and cannot be used as the keys of dictionaries (dictionary keys must be immutable in Python). Tuples are written as (1, 2, 3), are immutable and thus can be used as the keys of dictionaries, provided all elements of the tuple are immutable. The + operator can be used to concatenate two tuples, which does not directly modify their contents, but rather produces a new tuple containing the elements of both provided tuples. Thus, given the variable t initially equal to (1, 2, 3), executing t = t + (4, 5) first evaluates t + (4, 5), which yields (1, 2, 3, 4, 5), which is then assigned back to t, thereby effectively "modifying the contents" of t, while conforming to the immutable nature of tuple objects. Parentheses are optional for tuples in unambiguous contexts.

Python features sequence unpacking wherein multiple expressions, each evaluating to anything that can be assigned to (a variable, a writable property, etc.), are associated in an identical manner to that forming tuple literals and, as a whole, are put on the left-hand side of the equal sign in an assignment statement. The statement expects an iterable object on the right-hand side of the equal sign that produces the same number of values as the provided writable expressions when iterated through and will iterate through it, assigning each of the produced values to the corresponding expression on the left.

Python has a "string format" operator %. This functions analogously to printf format strings in C, e.g. "spam=%s eggs=%d" % ("blah", 2) evaluates to "spam=blah eggs=2". In Python 3 and 2.6+, this was supplemented by the format() method of the str class, e.g. "spam={0} eggs={1}".format("blah", 2). Python 3.6 added "f-strings": blah = "blah"; eggs = 2; f'spam={blah} eggs={eggs}'.

Strings in Python can be concatenated, by "adding" them (same operator as for adding integers and floats). E.g. "spam" + "eggs" returns "spameggs". Even if your strings contain numbers, they are still added as strings rather than integers. E.g. "2" + "2" returns "22".

Python has various kinds of string literals:

Strings delimited by single or double quote marks. Unlike in Unix shells, Perl and Perl-influenced languages, single quote marks and double quote marks function identically. Both kinds of string use the backslash (\) as an escape character. String interpolation became available in Python 3.6 as "formatted string literals".

Triple-quoted strings, which begin and end with a series of three single or double quote marks. They may span multiple lines and function like here documents in shells, Perl and Ruby.

Raw string varieties, denoted by prefixing the string literal with an r. Escape sequences are not interpreted; hence raw strings are useful where literal backslashes are common, such as regular expressions and Windows-style paths. Compare "@-quoting" in C#.

Python has array index and array slicing expressions on lists, denoted as a[key], a[start:stop] or a[start:stop:step]. Indexes are zero-based, and negative indexes are relative to the end. Slices take elements from the start index up to, but not including, the stop index. The third slice parameter, called step or stride, allows elements to be skipped and reversed. Slice indexes may be omitted, for example a[:] returns a copy of the entire list. Each element of a slice is a shallow copy.

In Python, a distinction between expressions and statements is rigidly enforced, in contrast to languages such as Common Lisp, Scheme, or Ruby. This leads to duplicating some functionality. For example:

List comprehensions vs. for-loops

Conditional expressions vs. if blocks

The eval() vs. exec() built-in functions (in Python 2, exec is a statement); the former is for expressions, the latter is for statements.

Statements cannot be a part of an expression, so list and other comprehensions or lambda expressions, all being expressions, cannot contain statements. A particular case of this is that an assignment statement such as a = 1 cannot form part of the conditional expression of a conditional statement. This has the advantage of avoiding a classic C error of mistaking an assignment operator = for an equality operator == in conditions: if (c = 1) { ... } is syntactically valid (but probably unintended) C code but if c = 1: ... causes a syntax error in Python.

## Methods

Methods on objects are functions attached to the object's class; the syntax instance.method(argument) is, for normal methods and functions, syntactic sugar for Class.method(instance, argument). Python methods have an explicit self parameter to access instance data, in contrast to the implicit self (or this) in some other object-oriented programming languages (e.g., C++, Java, Objective-C, or Ruby). Apart from this, Python also provides methods, often called dunder methods (due to their names beginning and ending with double-underscores), to allow user-defined classes to modify how they are handled by native operations such as length, comparison, in arithmetic operations, type conversion, and many more.

## Typing

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Python_3._The_standard_type_hierarchy.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Python_3._The_standard_type_hierarchy.png)

_The standard type hierarchy in Python 3_

Python uses duck typing and has typed objects but untyped variable names. Type constraints are not checked at compile time; rather, operations on an object may fail, signifying that the given object is not of a suitable type. Despite being dynamically-typed, Python is strongly-typed, forbidding operations that are not well-defined (for example, adding a number to a string) rather than silently attempting to make sense of them.

Python allows programmers to define their own types using classes, which are most often used for object-oriented programming. New instances of classes are constructed by calling the class (for example, SpamClass() or EggsClass()), and the classes are instances of the metaclass type (itself an instance of itself), allowing metaprogramming and reflection.

Before version 3.0, Python had two kinds of classes: old-style and new-style. The syntax of both styles is the same, the difference being whether the class object is inherited from, directly or indirectly (all new-style classes inherit from object and are instances of type). In versions of Python 2 from Python 2.2 onwards, both kinds of classes can be used. Old-style classes were eliminated in Python 3.0.

The long-term plan is to support gradual typing and from Python 3.5, the syntax of the language allows specifying static types but they are not checked in the default implementation, CPython. An experimental optional static type checker named mypy supports compile-time type checking.

Summary of Python 3's built-in types Type 	Mutability 	Description 	Syntax examples

bool 	immutable 	Boolean value 	True

False

bytearray 	mutable 	Sequence of bytes 	bytearray(b'Some ASCII')

bytearray(b"Some ASCII")

bytearray([119, 105, 107, 105])

bytes 	immutable 	Sequence of bytes 	b'Some ASCII'

b"Some ASCII"

bytes([119, 105, 107, 105])

complex 	immutable 	Complex number with real and imaginary parts 	3+2.7j

3 + 2.7j

dict 	mutable 	Associative array (or dictionary) of key and value pairs; can contain mixed types (keys and values), keys must be a hashable type 	{'key1': 1.0, 3: False}

{}

ellipsisa 	immutable 	An ellipsis placeholder to be used as an index in NumPy arrays 	...

Ellipsis

float 	immutable 	Double-precision floating-point number. The precision is machine-dependent but in practice is generally implemented as a 64-bit IEEE 754 number with 53 bits of precision.	

1.414

frozenset 	immutable 	Unordered set, contains no duplicates; can contain mixed types, if hashable 	frozenset([4.0, 'string', True])

int 	immutable 	Integer of unlimited magnitude 	42

list 	mutable 	List, can contain mixed types 	[4.0, 'string', True]

[]

NoneTypea 	immutable 	An object representing the absence of a value, often called null in other languages 	None

NotImplementedTypea 	immutable 	A placeholder that can be returned from overloaded operators to indicate unsupported operand types. 	NotImplemented

range 	immutable 	A Sequence of numbers commonly used for looping specific number of times in for loops 	range(1, 10)

range(10, -5, -2)

set 	mutable 	Unordered set, contains no duplicates; can contain mixed types, if hashable 	{4.0, 'string', True}

set()

str 	immutable 	A character string: sequence of Unicode codepoints 	'Wikipedia'

"Wikipedia"

"""Spanning

multiple

lines"""

tuple 	immutable 	Can contain mixed types 	(4.0, 'string', True)

('single element',)

()

^a Not directly accessible by name


## Arithmetic operations

Python has the usual symbols for arithmetic operators (+, -, *, /), the floor division operator // and the modulo operation % (where the remainder can be negative, e.g. 4 % -3 == -2). It also has ** for exponentiation, e.g. 5**3 == 125 and 9**0.5 == 3.0, and a matrix multiply operator @  These operators work like in traditional math; with the same precedence rules, the operators infix (+ and - can also be unary to represent positive and negative numbers respectively).

The division between integers produces floating-point results. The behavior of division has changed significantly over time:

Current Python (i.e. since 3.0) changed / to always be floating-point division, e.g. 5/2 == 2.5.

Python 2.2 changed integer division to round towards negative infinity, e.g. 7/3 == 2 and -7/3 == -3. The floor division // operator was introduced. So 7//3 == 2, -7//3 == -3, 7.5//3 == 2.0 and -7.5//3 == -3.0. Adding from __future__ import division causes a module to use Python 3.0 rules for division (see next).

Python 2.1 and earlier used C's division behavior. The / operator is integer division if both operands are integers, and floating-point division otherwise. Integer division rounds towards 0, e.g. 7/3 == 2 and -7/3 == -2.

In Python terms, / is true division (or simply division), and // is floor division. / before version 3.0 is classic division.

Rounding towards negative infinity, though different from most languages, adds consistency. For instance, it means that the equation (a + b)//b == a//b + 1 is always true. It also means that the equation b*(a//b) + a%b == a is valid for both positive and negative values of a. However, maintaining the validity of this equation means that while the result of a%b is, as expected, in the half-open interval [0, b), where b is a positive integer, it has to lie in the interval (b, 0] when b is negative.

Python provides a round function for rounding a float to the nearest integer. For tie-breaking, Python 3 uses round to even: round(1.5) and round(2.5) both produce 2. Versions before 3 used round-away-from-zero: round(0.5) is 1.0, round(-0.5) is −1.0.

Python allows boolean expressions with multiple equality relations in a manner that is consistent with general use in mathematics. For example, the expression a < b < c tests whether a is less than b and b is less than c. C-derived languages interpret this expression differently: in C, the expression would first evaluate a < b, resulting in 0 or 1, and that result would then be compared with c.

Python uses arbitrary-precision arithmetic for all integer operations. The Decimal type/class in the decimal module provides decimal floating-point numbers to a pre-defined arbitrary precision and several rounding modes. The Fraction class in the fractions module provides arbitrary precision for rational numbers.

Due to Python's extensive mathematics library, and the third-party library NumPy that further extends the native capabilities, it is frequently used as a scientific scripting language to aid in problems such as numerical data processing and manipulation.

## Programming examples

Hello world program:

```python
print('Hello, world!')
```

Program to calculate the factorial of a positive integer:

```python
n = int(input('Type a number, and its factorial will be printed: '))

if n < 0:
    raise ValueError('You must enter a non negative integer')

factorial = 1
for i in range(2, n + 1):
    factorial *= i

print(factorial)
```

## Libraries

Python's large standard library, commonly cited as one of its greatest strengths, provides tools suited to many tasks. For Internet-facing applications, many standard formats and protocols such as MIME and HTTP are supported. It includes modules for creating graphical user interfaces, connecting to relational databases, generating pseudorandom numbers, arithmetic with arbitrary-precision decimals, manipulating regular expressions, and unit testing.

Some parts of the standard library are covered by specifications (for example, the Web Server Gateway Interface (WSGI) implementation wsgiref follows PEP 333), but most modules are not. They are specified by their code, internal documentation, and test suites. However, because most of the standard library is cross-platform Python code, only a few modules need altering or rewriting for variant implementations.

As of March 2021, the Python Package Index (PyPI), the official repository for third-party Python software, contains over 290,000 packages with a wide range of functionality, including:

Automation

Data analytics

Databases

Documentation

Graphical user interfaces

Image processing

Machine learning

Mobile App

Multimedia

Computer networking

Scientific computing

System administration

Test frameworks

Text processing

Web frameworks

Web scraping

## Development environments

Most Python implementations (including CPython) include a read–eval–print loop (REPL), permitting them to function as a command line interpreter for which the user enters statements sequentially and receives results immediately.

Other shells, including IDLE and IPython, add further abilities such as improved auto-completion, session state retention and syntax highlighting.

As well as standard desktop integrated development environments, there are Web browser-based IDEs; SageMath (intended for developing science and math-related Python programs); PythonAnywhere, a browser-based IDE and hosting environment; and Canopy IDE, a commercial Python IDE emphasizing scientific computing.

## Implementations

#### Reference implementation

CPython is the reference implementation of Python. It is written in C, meeting the C89 standard with several select C99 features (with later C versions out, it's considered outdated; CPython includes its own C extensions, but third-party extensions are not limited to older C versions, can e.g. be implemented with C11 or C++). It compiles Python programs into an intermediate bytecode which is then executed by its virtual machine. CPython is distributed with a large standard library written in a mixture of C and native Python. It is available for many platforms, including Windows (starting with Python 3.9, the Python installer deliberately fails to install on Windows 7 and 8; Windows XP was supported until Python 3.5) and most modern Unix-like systems, including macOS (and Apple M1 Macs, since Python 3.9.1, with experimental installer) and unofficial support for e.g. VMS. Platform portability was one of its earliest priorities, during the Python 1 and 2 time-frame, even OS/2 and Solaris were supported; support has since been dropped for a lot of platforms.

### Other implementations

PyPy is a fast, compliant interpreter of Python 2.7 and 3.6. Its just-in-time compiler brings a significant speed improvement over CPython but several libraries written in C cannot be used with it.

Stackless Python is a significant fork of CPython that implements microthreads; it does not use the call stack in the same way, thus allowing massively concurrent programs. PyPy also has a stackless version.

MicroPython and CircuitPython are Python 3 variants optimized for microcontrollers, including Lego Mindstorms EV3.

Pyston is a variant of the Python runtime that uses just-in-time compilation to speed up the execution of Python programs.

Cinder is a performance-oriented fork of CPython 3.8 that contains a number of optimizations including bytecode inline caching, eager evaluation of coroutines, a method-at-a-time JIT and an experimental bytecode compiler.

### Unsupported implementations

Other just-in-time Python compilers have been developed, but are now unsupported:

Google began a project named Unladen Swallow in 2009, with the aim of speeding up the Python interpreter fivefold by using the LLVM, and of improving its multithreading ability to scale to thousands of cores, while ordinary implementations suffer from the global interpreter lock.

Psyco is a discontinued just-in-time specializing compiler that integrates with CPython and transforms bytecode to machine code at runtime. The emitted code is specialized for certain data types and is faster than the standard Python code. Psyco does not support Python 2.7 or later.

PyS60 was a Python 2 interpreter for Series 60 mobile phones released by Nokia in 2005. It implemented many of the modules from the standard library and some additional modules for integrating with the Symbian operating system. The Nokia N900 also supports Python with GTK widget libraries, enabling programs to be written and run on the target device.

## Cross-compilers to other languages

There are several compilers to high-level object languages, with either unrestricted Python, a restricted subset of Python, or a language similar to Python as the source language:

Cython compiles (a superset of) Python 2.7 to C (while the resulting code is also usable with Python 3 and also e.g. C++).

Nuitka compiles Python into C++.

Pythran compiles a subset of Python 3 to C++.

Pyrex (latest release in 2010) and Shed Skin (latest release in 2013) compile to C and C++ respectively.

Google's Grumpy (latest release in 2017) transpiles Python 2 to Go.

IronPython (now abandoned by Microsoft) allows running Python 2.7 programs on the .NET Common Language Runtime.

Jython compiles Python 2.7 to Java bytecode, allowing the use of the Java libraries from a Python program.

MyHDL is a Python-based hardware description language (HDL), that converts MyHDL code to Verilog or VHDL code.

Numba uses LLVM to compile a subset of Python to machine code.

Brython, Transcrypt and Pyjs (latest release in 2012) compile Python to JavaScript.

RPython can be compiled to C, and is used to build the PyPy interpreter of Python.

## Performance

A performance comparison of various Python implementations on a non-numerical (combinatorial) workload was presented at EuroSciPy '13. Python's performance compared to other programming languages is also benchmarked by The Computer Language Benchmarks Game.

## Development

Python's development is conducted largely through the Python Enhancement Proposal (PEP) process, the primary mechanism for proposing major new features, collecting community input on issues and documenting Python design decisions. Python coding style is covered in PEP 8. Outstanding PEPs are reviewed and commented on by the Python community and the steering council.

Enhancement of the language corresponds with development of the CPython reference implementation. The mailing list python-dev is the primary forum for the language's development. Specific issues are discussed in the Roundup bug tracker hosted at bugs.python.org. Development originally took place on a self-hosted source-code repository running Mercurial, until Python moved to GitHub in January 2017.

CPython's public releases come in three types, distinguished by which part of the version number is incremented:

Backward-incompatible versions, where code is expected to break and needs to be manually ported. The first part of the version number is incremented. These releases happen infrequently—version 3.0 was released 8 years after 2.0.

Major or "feature" releases, occurred about every 18 months but with the adoption of a yearly release cadence starting with Python 3.9 are expected to happen once a year. They are largely compatible but introduce new features. The second part of the version number is incremented. Each major version is supported by bugfixes for several years after its release.

Bugfix releases, which introduce no new features, occur about every 3 months and are made when a sufficient number of bugs have been fixed upstream since the last release. Security vulnerabilities are also patched in these releases. The third and final part of the version number is incremented.

Many alpha, beta, and release-candidates are also released as previews and for testing before final releases. Although there is a rough schedule for each release, they are often delayed if the code is not ready. Python's development team monitors the state of the code by running the large unit test suite during development.

The major academic conference on Python is PyCon. There are also special Python mentoring programmes, such as Pyladies.

Pythons 3.10 deprecates wstr (to be removed in Python 3.12; meaning Python extensions need to be modified by then), and also plans to add pattern matching to the language.

## API documentation generators

Tools that can generate documentation for Python API include pydoc (available as part of standard library), Sphinx, Pdoc and its forks, Doxygen and Graphviz, among others.

## Naming

Python's name is derived from the British comedy group Monty Python, whom Python creator Guido van Rossum enjoyed while developing the language. Monty Python references appear frequently in Python code and culture; for example, the metasyntactic variables often used in Python literature are spam and eggs instead of the traditional foo and bar. The official Python documentation also contains various references to Monty Python routines.

The prefix Py- is used to show that something is related to Python. Examples of the use of this prefix in names of Python applications or libraries include Pygame, a binding of SDL to Python (commonly used to create games); PyQt and PyGTK, which bind Qt and GTK to Python respectively; and PyPy, a Python implementation originally written in Python.

## Popularity

Since 2003, Python has consistently ranked in the top ten most popular programming languages in the TIOBE Programming Community Index where, as of February 2021, it is the third most popular language (behind Java, and C).] It was selected Programming Language of the Year (for "the highest rise in ratings in a year") in 2007, 2010, 2018, and 2020 (the only language to do so four times).

An empirical study found that scripting languages, such as Python, are more productive than conventional languages, such as C and Java, for programming problems involving string manipulation and search in a dictionary, and determined that memory consumption was often "better than Java and not much worse than C or C++".

Large organizations that use Python include Wikipedia, Google, Yahoo!, CERN, NASA, Facebook, Amazon, Instagram, Spotify and some smaller entities like ILM and ITA. The social news networking site Reddit was written mostly in Python.

## Uses

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Python_Powered.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/Python/Python_Powered.png)

_Python Powered services and platforms_

Python can serve as a scripting language for web applications, e.g., via mod_wsgi for the Apache web server. With Web Server Gateway Interface, a standard API has evolved to facilitate these applications. Web frameworks like Django, Pylons, Pyramid, TurboGears, web2py, Tornado, Flask, Bottle and Zope support developers in the design and maintenance of complex applications. Pyjs and IronPython can be used to develop the client-side of Ajax-based applications. SQLAlchemy can be used as a data mapper to a relational database. Twisted is a framework to program communications between computers, and is used (for example) by Dropbox.

Libraries such as NumPy, SciPy and Matplotlib allow the effective use of Python in scientific computing, with specialized libraries such as Biopython and Astropy providing domain-specific functionality. SageMath is a computer algebra system with a notebook interface programmable in Python: its library covers many aspects of mathematics, including algebra, combinatorics, numerical mathematics, number theory, and calculus. OpenCV has Python bindings with a rich set of features for computer vision and image processing.

Python is commonly used in artificial intelligence projects and machine learning projects with the help of libraries like TensorFlow, Keras, Pytorch and Scikit-learn. As a scripting language with modular architecture, simple syntax and rich text processing tools, Python is often used for natural language processing.

Python has been successfully embedded in many software products as a scripting language, including in finite element method software such as Abaqus, 3D parametric modeler like FreeCAD, 3D animation packages such as 3ds Max, Blender, Cinema 4D, Lightwave, Houdini, Maya, modo, MotionBuilder, Softimage, the visual effects compositor Nuke, 2D imaging programs like GIMP, Inkscape, Scribus and Paint Shop Pro, and musical notation programs like scorewriter and capella. GNU Debugger uses Python as a pretty printer to show complex structures such as C++ containers. Esri promotes Python as the best choice for writing scripts in ArcGIS. It has also been used in several video games, and has been adopted as first of the three available programming languages in Google App Engine, the other two being Java and Go.

Many operating systems include Python as a standard component. It ships with most Linux distributions, AmigaOS 4 (using Python 2.7), FreeBSD (as a package), NetBSD, OpenBSD (as a package) and macOS and can be used from the command line (terminal). Many Linux distributions use installers written in Python: Ubuntu uses the Ubiquity installer, while Red Hat Linux and Fedora use the Anaconda installer. Gentoo Linux uses Python in its package management system, Portage.

Python is used extensively in the information security industry, including in exploit development.

Most of the Sugar software for the One Laptop per Child XO, now developed at Sugar Labs, is written in Python. The Raspberry Pi single-board computer project has adopted Python as its main user-programming language.

LibreOffice includes Python, and intends to replace Java with Python. Its Python Scripting Provider is a core feature since Version 4.0 from 7 February 2013.

## Languages influenced by Python

Python's design and philosophy have influenced many other programming languages:

Boo uses indentation, a similar syntax, and a similar object model.

Cobra uses indentation and a similar syntax, and its Acknowledgements document lists Python first among languages that influenced it.

CoffeeScript, a programming language that cross-compiles to JavaScript, has Python-inspired syntax.

ECMAScript/JavaScript borrowed iterators and generators from Python.

GDScript, a scripting language very similar to Python, built-in to the Godot game engine.

Go is designed for the "speed of working in a dynamic language like Python" and shares the same syntax for slicing arrays.

Groovy was motivated by the desire to bring the Python design philosophy to Java.

Julia was designed to be "as usable for general programming as Python".

Nim uses indentation and similar syntax

Ruby's creator, Yukihiro Matsumoto, has said: "I wanted a scripting language that was more powerful than Perl, and more object-oriented than Python. That's why I decided to design my own language."

Swift, a programming language developed by Apple, has some Python-inspired syntax.

Python's development practices have also been emulated by other languages. For example, the practice of requiring a document describing the rationale for, and issues surrounding, a change to the language (in Python, a PEP) is also used in Tcl, Erlang, and Swift.

**This article is a modified copy of its Wikipedia counterpart and needs to be rewritten for originality.**

***

## Sources

[Wikipedia - Python (programming language)](https://en.wikipedia.org/wiki/Python_(programming_language))

[Wikipedia - Python syntax and semantics](https://en.wikipedia.org/wiki/Python_syntax_and_semantics)

Other sources are needed, and this article needs LOTS of improvement and original work to prevent it from being a copy and paste from Wikipedia.

***

## Article info

**Written on:** `2021 Monday September 27th at 1:58 pm`

**Last revised on:** `2021 Monday September 27th at 1:58 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article language:** `English (US) / Markdown`

**Line count (including blank lines and compiler line):** `1,290`

**Article version:** `1 (2021 Monday September 27th at 1:58 pm)`

***

<!-- Tools

Quick copy and paste

https://github.com/seanpm2001/WacOS/wiki/

!-->
