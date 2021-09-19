
***

# Objective-C++

[Go back (WacOS/Wiki/)](https://github.com/seanpm2001/WacOS/wiki)

Objective-C++ is a modified version of Objective-C with compatibility with C and C++ although not designed by Apple, it is a recognized language and variant.

Since I don't have any direct examples of the language, I will show Objective-C 2.0 examples here.

***

## Objective-C 2.0

At the 2006 Worldwide Developers Conference, Apple announced the release of "Objective-C 2.0," a revision of the Objective-C language to include "modern garbage collection, syntax enhancements, runtime performance improvements, and 64-bit support". Mac OS X v10.5, released in October 2007, included an Objective-C 2.0 compiler. GCC 4.6 supports many new Objective-C features, such as declared and synthesized properties, dot syntax, fast enumeration, optional protocol methods, method/protocol/class attributes, class extensions, and a new GNU Objective-C runtime API.

The naming Objective-C 2.0 represents a break in the versioning system of the language, as the last Objective-C version for NeXT was "objc4". This project name was kept in the last release of legacy Objective-C runtime source code in Mac OS X Leopard (10.5).

### Properties

Properties in Objective-C work like this:

```objective-c
@interface Person : NSObject {
@public
  NSString *name;
@private
  int age;
}

@property(copy) NSString *name;
@property(readonly) int age;

- (id)initWithAge:(int)age;
@end
```

Properties are implemented with the @synthesize keyword like so:

```objective-c
@implementation Person
@synthesize name;

- (id)initWithAge:(int)initAge {
  self = [super init];
  if (self) {
    // NOTE: direct instance variable assignment, not property setter
    age = initAge;
  }
  return self;
}

- (int)age {
  return age;
}
@end
```

Properties can be accessed using the traditional message passing syntax, dot notation, or, in Key-Value Coding, by name via the "valueForKey:"/"setValue:forKey:" methods.

```objective-c
Person *aPerson = [[Person alloc] initWithAge:53];
aPerson.name = @"Steve"; // NOTE: dot notation, uses synthesized setter,
                         // equivalent to [aPerson setName: @"Steve"];
NSLog(@"Access by message (%@), dot notation(%@), property name(% @) and
          direct instance variable access(% @) ",
              [aPerson name],
      aPerson.name, [aPerson valueForKey:@"name"], aPerson -> name);
```

In order to use dot notation to invoke property accessors within an instance method, the "self" keyword should be used:

```objective-c
- (void)introduceMyselfWithProperties:(BOOL)useGetter {
  NSLog(@"Hi, my name is %@.", (useGetter ? self.name : name));
  // NOTE: getter vs. ivar access
}
```

A class or protocol's properties may be dynamically introspected.

```objective-c
int i;
int propertyCount = 0;
objc_property_t *propertyList =
    class_copyPropertyList([aPerson class], &propertyCount);

for (i = 0; i < propertyCount; i++) {
  objc_property_t *thisProperty = propertyList + i;
  const char *propertyName = property_getName(*thisProperty);
  NSLog(@"Person has a property: '%s'", propertyName);
}
```

***

### Fast enumeration

Objective-C2 enables fast enumeration through the NSEnumerator library. You can take hold of this power like so:

```objective-c
// Using NSEnumerator
NSEnumerator *enumerator = [thePeople objectEnumerator];
Person *p;

while ((p = [enumerator nextObject]) != nil) {
  NSLog(@"%@ is %i years old.", [p name], [p age]);
}

// Using indexes
for (int i = 0; i < [thePeople count]; i++) {
  Person *p = [thePeople objectAtIndex:i];
  NSLog(@"%@ is %i years old.", [p name], [p age]);
}

// Using fast enumeration
for (Person *p in thePeople) {
  NSLog(@"%@ is %i years old.", [p name], [p age]);
}
```

***

### Blocks

Code blocks can be written in Objective-C like so:

```objective-c
#include <stdio.h>
#include <Block.h>
typedef int (^IntBlock)();

IntBlock MakeCounter(int start, int increment) {
  __block int i = start;

  return Block_copy( ^ {
    int ret = i;
    i += increment;
    return ret;
  });

}

int main(void) {
  IntBlock mycounter = MakeCounter(5, 2);
  printf("First call: %d\n", mycounter());
  printf("Second call: %d\n", mycounter());
  printf("Third call: %d\n", mycounter());

  /* because it was copied, it must also be released */
  Block_release(mycounter);

  return 0;
}
/* Output:
  First call: 5
  Second call: 7
  Third call: 9
*/
```

## Modern Objective-C

Apple has made many changes to Objective-C over the years. Here are some examples of "modern" Objective-C

### Literals

Example without literals:

```objective-c
NSArray *myArray = [NSArray arrayWithObjects:object1,object2,object3,nil];
NSDictionary *myDictionary1 = [NSDictionary dictionaryWithObject:someObject forKey:@"key"];
NSDictionary *myDictionary2 = [NSDictionary dictionaryWithObjectsAndKeys:object1, key1, object2, key2, nil];
NSNumber *myNumber = [NSNumber numberWithInt:myInt];
NSNumber *mySumNumber= [NSNumber numberWithInt:(2 + 3)];
NSNumber *myBoolNumber = [NSNumber numberWithBool:YES];
```

Examples with literals:

```objective-c
NSArray *myArray = @[ object1, object2, object3 ];
NSDictionary *myDictionary1 = @{ @"key" : someObject };
NSDictionary *myDictionary2 = @{ key1: object1, key2: object2 };
NSNumber *myNumber = @(myInt);
NSNumber *mySumNumber = @(2+3);
NSNumber *myBoolNumber = @YES;
NSNumber *myIntegerNumber = @8;
```

***

### Subscripting

Modern Objective-C adds support for Subscripting. Here is a source code example without subscripting:

```objective-c
id object1 = [someArray objectAtIndex:0];
id object2 = [someDictionary objectForKey:@"key"];
[someMutableArray replaceObjectAtIndex:0 withObject:object3];
[someMutableDictionary setObject:object4 forKey:@"key"];
```

And here is an example with subscripting:

```objective-c
id object1 = someArray[0];
id object2 = someDictionary[@"key"];
someMutableArray[0] = object3;
someMutableDictionary[@"key"] = object4;
```

***

## Objective-C 1997

After the purchase of NeXT by Apple, attempts were made to make the language more acceptable to programmers more familiar with Java than Smalltalk. One of these attempts was introducing what was dubbed "Modern Syntax" for Objective-C at the time. (as opposed to the current, "classic" syntax). There was no change in behaviour, this was merely an alternative syntax. Instead of writing a method invocation like:

```objective-c
object = [[MyClass alloc] init];
[object firstLabel: param1 secondLabel: param2];
```

It was instead written as:

```objective-c
object = (MyClass.alloc).init;
object.labels ( param1, param2 );
```

Similarly, declarations went from the form:

```objective-c
-(void) firstLabel: (int)param1 secondLabel: (int)param2;
```

to:

```objective-c
-(void) labels ( int param1, int param2 );
```

This "modern" syntax is no longer supported in current dialects of the Objective-C language.

[Source: Wikipedia](https://en.wikipedia.org/wiki/Objective-C)

<!-- [Source: RosettaCode](http://rosettacode.org/wiki/Hello_world/Text#Swift) !-->

## Use by Apple

Apple used the language heavily on their systems from Mac System Software 1 to MacOS 10.10/iOS 8. Apple still uses and supports the language, but recommends using the [Swift programming language now](https://github.com/seanpm2001/WacOS/wiki/Swift-(programming-language))

## Use by WacOS

WacOS uses Objective-C++ for certain functions to maintain Apple compatibility. These functions include:

* Recreating some APIs

* General system development

_This article on programming languages is a stub. You can help by expanding it!_

WacOS also uses [Objective-C](https://github.com/seanpm2001/WacOS/wiki/Objective-C/) in some areas for the same reasons.

***

## Sources

[Source: Wikipedia - Objective-C](https://en.wikipedia.org/wiki/Objective-C/)

More sources needed, Wikipedia cannot be the only source, as Wikipedia isn't really something to use as a source. More credible sources are needed.

***

## Article info

**Written on:** `2021, Sunday, September 19th at 4:50 pm)`

**Last revised on:** `2021, Sunday, September 19th at 4:50 pm)`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Sunday September 19th at 4:50 pm)`

***
