
***

# Objective-C

[Go back (WacOS/Wiki/)](https://github.com/seanpm2001/WacOS/wiki)

Objective-C was Apples default programming language for programming their applications until 2014, with the release of Swift. Objective-C is still used to this day, but the language itself hasn't been updated since 1984.

***

## Messages

Writing messages in Objective-C is done as so:

```objective-c
[obj method:argument];
```

In C++ this would be written as:

```cpp
obj->method(argument);
```

***

## Interface

This is the interface for Objective-C:

```objective-c
@interface classname : superclassname {
  // instance variables
}
+ classMethod1;
+ (return_type)classMethod2;
+ (return_type)classMethod3:(param1_type)param1_varName;

- (return_type)instanceMethod1With1Parameter:(param1_type)param1_varName;
- (return_type)instanceMethod2With2Parameters:(param1_type)param1_varName
                              param2_callName:(param2_type)param2_varName;
@end
```

In C++ this would be written as:

```cpp
class classname : public superclassname {
protected:
  // instance variables

public:
  // Class (static) functions
  static void *classMethod1();
  static return_type classMethod2();
  static return_type classMethod3(param1_type param1_varName);

  // Instance (member) functions
  return_type instanceMethod1With1Parameter(param1_type param1_varName);
  return_type
  instanceMethod2With2Parameters(param1_type param1_varName,
                                 param2_type param2_varName = default);
};
```

Return types can be any standard C type, a pointer to a generic Objective-C object, a pointer to a specific type of object such as NSArray *, NSImage *, or NSString *, or a pointer to the class to which the method belongs (instancetype). The default return type is the generic Objective-C type id.

Method arguments begin with a name labeling the argument that is part of the method name, followed by a colon followed by the expected argument type in parentheses and the argument name. The label can be omitted. 

```objective-c
- (void)setRangeStart:(int)start end:(int)end;
- (void)importDocumentWithName:(NSString *)name
      withSpecifiedPreferences:(Preferences *)prefs
                    beforePage:(int)insertPage;
```

***

## Implementation

This is the way you use implementations in Objective-C

```objective-c
@implementation classname
+ (return_type)classMethod {
  // implementation
}
- (return_type)instanceMethod {
  // implementation
}
@end
```

A similar way is:

```objective-c
- (int)method:(int)i {
  return [self square_root:i];
}
```

Which is equivalent to the C source code of:

```c
int function(int i) {
  return square_root(i);
}
```

Objective-Cs syntax also allows pseudo naming of arguments

```objective-c
- (void)changeColorToRed:(float)red green:(float)green blue:(float)blue {
  //... Implementation ...
}

// Called like so:
[myColor changeColorToRed:5.0 green:2.0 blue:6.0];
```

***

## Init

You can make installations through Init (process 1) like so:

```objective-c
- (id)init {
    self = [super init];
    if (self) {
        // perform initialization of object here
    }
    return self;
}
```

***

## Protocols

Here are some standard protocols in Objective-C

```objective-c
@protocol NSLocking
- (void)lock;
- (void)unlock;
@end
```

Here is the class definition that the protocol was implemented:

```objective-c
@interface NSLock : NSObject <NSLocking>
// ...
@end
```

## Dynamic typing

Objective-C has many methods of dynamic typing. Here are some examples:

```objective-c
- (void)setMyValue:(id)foo;
```

In the above statement, foo may be of any class.

```objective-c
- (void)setMyValue:(id<NSCopying>)foo;
```

In the above statement, foo may be an instance of any class that conforms to the NSCopying protocol.

```objective-c
- (void)setMyValue:(NSNumber *)foo;
```

In the above statement, foo must be an instance of the NSNumber class.

```objective-c
- (void)setMyValue:(NSNumber<NSCopying> *)foo;
```

In the above statement, foo must be an instance of the NSNumber class, and it must conform to the NSCopying protocol.

In Objective-C, all objects are represented as pointers, and static initialization is not allowed. The simplest object is the type that id (objc_obj *) points to, which only has an isa pointer describing its class. Other types from C, like values and structs, are unchanged because they are not part of the object system. This decision differs from the C++ object model, where structs and classes are united. 

***

## Forwarding messages

Forwarding messages are written like this:

```objective-c
- (retval_t)forward:(SEL)sel args:(arglist_t)args; // with GCC
- (id)forward:(SEL)sel args:(marg_list)args; // with NeXT/Apple systems
```

Action messages are written as so:

```objective-c
- (retval_t)performv:(SEL)sel args:(arglist_t)args; // with GCC
- (id)performv:(SEL)sel args:(marg_list)args; // with NeXT/Apple systems
```

To use these methods as a functional program, you will need some header files.

```
forwarder.h
```

```objective-c
#import <objc/Object.h>

@interface Forwarder : Object {
  id recipient; // The object we want to forward the message to.
}

// Accessor methods.
- (id)recipient;
- (id)setRecipient:(id)_recipient;
@end
```

```
forwarder.m
```

```objective-c
#import "Forwarder.h"

@implementation Forwarder
- (retval_t)forward:(SEL)sel args:(arglist_t)args {
  /*
  * Check whether the recipient actually responds to the message.
  * This may or may not be desirable, for example, if a recipient
  * in turn does not respond to the message, it might do forwarding
  * itself.
  */
  if ([recipient respondsToSelector:sel]) {
    return [recipient performv:sel args:args];
  } else {
    return [self error:"Recipient does not respond"];
  }
}

- (id)setRecipient:(id)_recipient {
  [recipient autorelease];
  recipient = [_recipient retain];
  return self;
}

- (id)recipient {
  return recipient;
}
@end
```

```
recipient.h
```

```objective-c
#import <objc/Object.h>

// A simple Recipient object.
@interface Recipient : Object
- (id)hello;
@end
```

```
recipient.m
```

```objective-c
#import "Recipient.h"

@implementation Recipient

- (id)hello {
  printf("Recipient says hello!\n");

  return self;
}

@end
```

```
main.m
```

```objective-c
#import "Forwarder.h"
#import "Recipient.h"

int main(void) {
  Forwarder *forwarder = [Forwarder new];
  Recipient *recipient = [Recipient new];

  [forwarder setRecipient:recipient]; // Set the recipient.
  /*
  * Observe forwarder does not respond to a hello message! It will
  * be forwarded. All unrecognized methods will be forwarded to
  * the recipient
  * (if the recipient responds to them, as written in the Forwarder)
  */
  [forwarder hello];

  [recipient release];
  [forwarder release];

  return 0;
}
```

***

## Categories

Categories can be used like this via a multi-header system.

```
Integer.h
```

```objective-c
#import <objc/Object.h>

@interface Integer : Object {
  int integer;
}

- (int)integer;
- (id)integer:(int)_integer;
@end
```

```
integer.m
```

```objective-c
#import "Integer.h"

@implementation Integer
- (int) integer {
  return integer;
}

- (id) integer: (int) _integer {
  integer = _integer;
  return self;
}
@end
```

```
Integer+Arithmetic.h
```

```objective-c
#import "Integer.h"

@interface Integer (Arithmetic)
- (id) add: (Integer *) addend;
- (id) sub: (Integer *) subtrahend;
@end
```

```
Integer+Arithmetic.m
```

```objective-c
# import "Integer+Arithmetic.h"

@implementation Integer (Arithmetic)
- (id) add: (Integer *) addend {
  return [self integer: [self integer] + [addend integer]];
}

- (id) sub: (Integer *) subtrahend {
  return [self integer: [self integer] - [subtrahend integer]];
}
@end

```
Integer+Display.h
```

```objective-c
#import "Integer.h"

@interface Integer (Display)
- (id) showstars;
- (id) showint;
@end
```

```
Integer+Display.m
```

```objective-c
# import "Integer+Display.h"

@implementation Integer (Display)
- (id) showstars {
  int i, x = [self integer];
  for (i = 0; i < x; i++) {
    printf("*");
  }
  printf("\n");

  return self;
}

- (id) showint {
  printf("%d\n", [self integer]);

  return self;
}
@end
```

```
main.m
```

```objective-c
#import "Integer.h"
#import "Integer+Arithmetic.h"
#import "Integer+Display.h"

int main(void) {
  Integer *num1 = [Integer new], *num2 = [Integer new];
  int x;

  printf("Enter an integer: ");
  scanf("%d", &x);

  [num1 integer:x];
  [num1 showstars];

  printf("Enter an integer: ");
  scanf("%d", &x);

  [num2 integer:x];
  [num2 showstars];

  [num1 add:num2];
  [num1 showint];

  return 0;
}
```

***

## Posing

Posing in Objective-C

```objective-c
@interface CustomNSApplication : NSApplication
@end

@implementation CustomNSApplication
- (void) setMainMenu: (NSMenu*) menu {
  // do something with menu
}
@end

class_poseAs ([CustomNSApplication class], [NSApplication class]);
```

***

## Hello World

A hello world in Objective-C can be done like so:

```objective-c
// FILE: hello.m
#import <Foundation/Foundation.h>
int main (int argc, const char * argv[])
{
    /* my first program in Objective-C */
    NSLog(@"Hello, World! \n");
    return 0;
}
```

Compiling it in Linux/GCC can be done like so (don't run this as a shell script)

```sh
# Compile Command Line for gcc and MinGW Compiler:
$ gcc \
    $(gnustep-config --objc-flags) \
    -o hello \
    hello.m \
    -L /GNUstep/System/Library/Libraries \
    -lobjc \
    -lgnustep-base

$ ./hello
```


[Source: Wikipedia](https://en.wikipedia.org/wiki/Objective-C)

<!-- [Source: RosettaCode](http://rosettacode.org/wiki/Hello_world/Text#Swift) !-->

## Use by Apple

Apple used the language heavily on their systems from Mac System Software 1 to MacOS 10.10/iOS 8. Apple still uses and supports the language, but recommends using the [Swift programming language now](https://github.com/seanpm2001/WacOS/wiki/Swift-(programming-language))

## Use by WacOS

WacOS uses Objective-C for certain functions to maintain Apple compatibility. These functions include:

* Recreating some APIs

* General system development

_This article on programming languages is a stub. You can help by expanding it!_

WacOS also uses [Objective-C++](https://github.com/seanpm2001/WacOS/wiki/Objective-CPP/) in some areas for the same reasons.

***

## Sources

[Source: Wikipedia - Objective-C](https://en.wikipedia.org/wiki/Objective-C/)

More sources needed, Wikipedia cannot be the only source, as Wikipedia isn't really something to use as a source. More credible sources are needed.

***

## Article info

**Written on:** `2021, Sunday, September 19th at 4:34 pm)`

**Last revised on:** `2021, Sunday, September 19th at 4:34 pm)`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Sunday September 19th at 4:34 pm)`

***
