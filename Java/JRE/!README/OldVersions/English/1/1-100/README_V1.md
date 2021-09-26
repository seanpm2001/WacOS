
***

# Java Runtime Environment (JRE)

WacOS will optionally allow you to install most versions of Java along with the system, either at installation or after. A JRE will be maintained here that handles Java throughout the system.

This feature is currently in development, and is in the planning phase.

## Java packages

The following Java packages are supported

### JDK

Java Development Kit (JDK) packages are listed here

JDK 13 for Linux.??

JDK 12 for Linux.??

JDK 11 for Linux.??

JDK 10 for Linux.??

JDK 9 for Linux.??

JDK 8 for Linux.??

JDK 7 for Linux.??

JDK 6 for Linux.??

### JSE

The following Java Standard Edition environment packages are listed here

JSE 13 for Linux.??

JSE 12 for Linux.??

JSE 11 for Linux.??

JSE 10 for Linux.??

JSE 9 for Linux.??

JSE 8 for Linux.??

JSE 7 for Linux.??

JSE 6 for Linux.??

### Test program

Use this Hello World program to test your installation:

```java
public class HelloWorldApp {
    public static void main(String[] args) {
        System.out.println("Hello World!"); // Prints the string to the console.
    }
}
```

Or do a more advanced test:

```java
// This is an example of a single line comment using two slashes

/*
 * This is an example of a multiple line comment using the slash and asterisk.
 * This type of comment can be used to hold a lot of information or deactivate
 * code, but it is very important to remember to close the comment.
 */

package fibsandlies;

import java.util.Map;
import java.util.HashMap;

/**
 * This is an example of a Javadoc comment; Javadoc can compile documentation
 * from this text. Javadoc comments must immediately precede the class, method,
 * or field being documented.
 * @author Wikipedia Volunteers
 */
public class FibCalculator extends Fibonacci implements Calculator {
    private static Map<Integer, Integer> memoized = new HashMap<>();

    /*
     * The main method written as follows is used by the JVM as a starting point
     * for the program.
     */
    public static void main(String[] args) {
        memoized.put(1, 1);
        memoized.put(2, 1);
        System.out.println(fibonacci(12)); // Get the 12th Fibonacci number and print to console
    }

    /**
     * An example of a method written in Java, wrapped in a class.
     * Given a non-negative number FIBINDEX, returns
     * the Nth Fibonacci number, where N equals FIBINDEX.
     * 
     * @param fibIndex The index of the Fibonacci number
     * @return the Fibonacci number
     */
    public static int fibonacci(int fibIndex) {
        if (memoized.containsKey(fibIndex)) return memoized.get(fibIndex);
        else {
            int answer = fibonacci(fibIndex - 1) + fibonacci(fibIndex - 2);
            memoized.put(fibIndex, answer);
            return answer;
        }
    }
}
```

***

## File info

**File type:** `Markdown document (*.md *.mkd *.markdown)`

**File version:** `1 (Saturday, 2021 September 25th at 8:39 pm)`

**Line count (including blank lines and compiler line):** `131`

***
