
***

# AppleScript

[Go back (WacOS/Wiki/)](https://github.com/seanpm2001/WacOS/wiki)

![Image](https://github.com/seanpm2001/WacOS/blob/master/Graphics/AppleScript/AppleScript_Editor_Logo.png)

The icon for the AppleScript programming language [view it here](https://github.com/seanpm2001/WacOS/blob/master/Graphics/AppleScript/AppleScript_Editor_Logo.png)

AppleScript is a scripting language by Apple Inc for easy automated scripting of MacOS programs, starting from System 7. Its last release was the 2.5 release, released on 2014 October 16th.

## Comments

This is how to make source code comments in AppleScript.

```applescript
--This is a one line comment
# So is this! (in Mac OS X Leopard or later)
```

```applescript
(* This is a
multiple
line
comment *)
```

## Hello world program

This is the standard Hello world program in AppleScript, which can be displayed 3 different ways.

```applescript
display dialog "Hello, world!" -- a modal window with "OK" and "Cancel" buttons
-- or
display alert "Hello, world!" -- a modal window with a single "OK" button and an icon
representing the app displaying the alert
-- or
say "Hello, world!" -- an audio message using a synthesized computer voice
```

Here is an alternative Hello World program:

```applescript
display alert "Hello, world!" buttons {"Rudely decline", "Happily accept"}
set theAnswer to button returned of the result
if theAnswer is "Happily accept" then
beep 5
else
say "Piffle!"
end if
```

## Other programs

AppleScript uses the ¬ character.

### Simple dialog program

```applescript
-- Dialog
set dialogReply to display dialog "Dialog Text" ¬
default answer "Text Answer" ¬
hidden answer false ¬
buttons {"Skip", "Okay", "Cancel"} ¬
default button "Okay" ¬
cancel button "Skip" ¬
with title "Dialog Window Title" ¬
with icon note ¬
giving up after 15
```

### Simple list making and entry grabbing

```applescript
-- Choose from list
set chosenListItem to choose from list {"A", "B", "3"} ¬
with title "List Title" ¬
with prompt "Prompt Text" ¬
default items "B" ¬
OK button name "Looks Good!" ¬
cancel button name "Nope, try again" ¬
multiple selections allowed false ¬
with empty selection allowed
```

### Alerting message

```applescript
-- Alert
set resultAlertReply to display alert "Alert Text" ¬
as warning ¬
buttons {"Skip", "Okay", "Cancel"} ¬
default button 2 ¬
cancel button 1 ¬
giving up after 2
```

### Printing pages from a document

```applescript
print page 1
print document 2
print pages 1 thru 5 of document 2
```

### Telling Microsoft word to quit

```applescript
tell application "Microsoft Word"
quit
end tell
```

#### Alternative method 1

```applescript
tell application "Microsoft Word" to quit
```

#### Alternative method 2

```applescript
quit application "Microsoft Word"
```

### An example of the object hierarchy

```applescript
tell application "QuarkXPress"
  tell document 1
    tell page 2
      tell text box 1
        set word 5 to "Apple"
      end tell
    end tell
  end tell
end tell
```

### Getting a selection of a TIFF image

```applescript
pixel 7 of row 3 of TIFF image "my bitmap"
```

#### Pseudocode of how to do this in another language

```applescript
getTIFF("my bitmap").getRow(3).getPixel(7);
```

### A failsafe calculator

```applescript
tell application "Finder"
-- Set variables
set the1 to text returned of (display dialog "1st" default answer "Number here" buttons
{"Continue"} default button 1)
set the2 to text returned of (display dialog "2nd" default answer "Number here" buttons
{"Continue"} default button 1)
try
set the1 to the1 as integer
set the2 to the2 as integer
on error
display dialog "You may only input numbers into a calculator." with title "ERROR"
buttons {"OK"} default button 1
return
end try
-- Add?
if the button returned of (display dialog "Add?" buttons {"No", "Yes"} default button 2)
is "Yes" then
set ans to (the1 + the2)
display dialog ans with title "Answer" buttons {"OK"} default button 1
say ans
-- Subtract?
else if the button returned of (display dialog "Subtract?" buttons {"No", "Yes"} default
button 2) is "Yes" then
set ans to (the1 - the2)
display dialog ans with title "Answer" buttons {"OK"} default button 1
say ans
-- Multiply?
else if the button returned of (display dialog "Multiply?" buttons {"No", "Yes"} default
button 2) is "Yes" then
set ans to (the1 * the2)
display dialog ans with title "Answer" buttons {"OK"} default button 1
say ans
-- Divide?
else if the button returned of (display dialog "Divide?" buttons {"No", "Yes"} default
button 2) is "Yes" then
set ans to (the1 / the2)
display dialog ans with title "Answer" buttons {"OK"} default button 1
say ans
else
delay 1
say "You haven't selected a function. The operation has cancelled."
end if
end tell
```

### A simple username and password program

```applescript
tell application "Finder"
set passAns to "app123"
set userAns to "John"
if the text returned of (display dialog "Username" default answer "") is userAns then
display dialog "Correct" buttons {"Continue"} default button 1
if the text returned of (display dialog "Username : John" & return & "Password"
default answer "" buttons {"Continue"} default button 1 with hidden answer) is passAns then
display dialog "Access granted" buttons {"OK"} default button 1
else
display dialog "Incorrect password" buttons {"OK"} default button 1
end if
else
display dialog "Incorrect username" buttons {"OK"} default button 1
end if
end tell
```

### Conditionals

```applescript
-- Simple conditional
if x < 1000 then set x to x + 1
-- Compound conditional
if x is greater than 3 then
-- commands
else
-- other commands
end if
```

### Forever loop

```applescript
repeat
-- commands to be repeated
end repeat
```

### Repeat 10 times loop

```applescript
repeat 10 times
-- commands to be repeated
end repeat
```

### Repeat while loop

```applescript
set x to 5
repeat while x > 0
set x to x - 1
end repeat
set x to 5
repeat until x ≤ 0
set x to x - 1
end repeat
```

### Repeat variable loop

```applescript
-- repeat the block 2000 times, i gets all values from 1 to 2000
repeat with i from 1 to 2000
-- commands to be repeated
end repeat
-- repeat the block 4 times, i gets values 100, 75, 50 and 25
repeat with i from 100 to 25 by -25
-- commands to be repeated
end repeat
```

### Enumerating a list

```applescript
set total to 0
repeat with loopVariable in {1, 2, 3, 4, 5}
set total to total + loopVariable
end repeat
```

### Application

```applescript
-- Simple form
tell application "Safari" to activate
-- Compound
tell application "MyApp"
-- commands for app
end tell
```

#### Error handling

```applescript
try
-- commands to be tested
on error
-- error commands
end try
```

### Function handler

```applescript
on
myFunction(parameters...)
-- subroutine
commands
end myFunction
```

#### Folder actions block

```applescript
on adding folder items to thisFolder after
receiving theseItems
-- commands to apply to the folder or
items
end adding folder items to
```

#### Run handler

```applescript
on run
--
commands
end run
```

### Handler with labeled parameters

```applescript
on rock around the clock
display dialog (clock as
string)
end rock
-- called with:
rock around the current date
```

#### Handler using "to" and label parameters

```applescript
to check for yourNumber from bottom thru top
if bottom ≤ yourNumber and yourNumber ≤ top
then
display dialog "Congratulations! You
scored."
end if
end check
--called with:
check for 8 from 7 thru 10
```

### Open handler

```applescript
on open theItems
repeat with thisItem in theItems
tell application "Finder" to update thisItem
end repeat
end open
```

### Idle handler

```applescript
on idle
--code to execute when the script's execution has completed
return 60 -- number of seconds to pause before executing idle handler again
end idle
```

### Quit handler

```applescript
on quit
--commands to execute before the script quits
continue quit -- required for the script to actually quit
end quit
```

### Script objects

```applescript
script scriptName
-- commands and handlers specific to the script
end script
```

### Miscellaneous information

```applescript
set variable1 to 1 -- create an integer variable called variable1
set variable2 to "Hello" -- create a text variable called variable2
copy {17, "doubleday"} to variable3 -- create a list variable called variable3
set {variable4, variable5} to variable3 -- copy the list items of variable3 into separate
variables variable4 and variable5
set variable6 to script myScript -- set a variable to an instance of a script
```

#### Cannot call subroutines at this time

```applescript
tell application "Finder"
set x to my myHandler()
-- or
set x to myHandler() of me
end tell
on myHandler()
--commands
end myHandler
```

#### Improved performance example

```applescript
tell application "Finder"
set anyNumber to my (random number from 5 to 50)
end tell
```

[Source: Wikipedia](https://en.wikipedia.org/wiki/AppleScript/)

## Use in WacOS

The WacOS project plans to have an open-source reverse-engineered implementation of AppleScript to use for legacy purposes in the system.

## Use by Apple

AppleScript is used primarily to make MacOS programs since System 7, and is still being used to this day. I am not sure if it is being run on other Apple systems though.

_This article on programming languages is a stub. You can help by expanding it!_

***

## Article info

**Written on:** `2021, Wednesday September 15th at 3:18 pm)`

**Last revised on:** `2021, Wednesday September 15th at 3:18 pm)`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021, Wednesday September 15th at 3:18 pm)`

***
