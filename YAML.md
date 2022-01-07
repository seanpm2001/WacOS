
***

# YAML

[Go back (WacOS/Wiki/)](https://github.com/seanpm2001/WacOS/wiki)

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/YAML/YAML_Logo.svg](https://github.com/seanpm2001/WacOS/blob/master/Graphics/YAML/YAML_Logo.svg)

YAML (YAML Ain't Markup Language) is a human readable data serialization computer language.

The latest stable version of YAML is 1.2 (third edition) which was released on 2009 October 1st.

## Basic components

Here are some basic examples of YAML

```yaml
--- # Favorite movies
- Casablanca
- North by Northwest
- The Man Who Wasn't There
```

```yaml
--- # Shopping list
[milk, pumpkin pie, eggs, juice]
```

```yaml
--- # Indented Block
  name: John Smith
  age: 33
--- # Inline Block
{name: John Smith, age: 33}
```

```yaml
data: |
   There once was a tall man from Ealing
   Who got on a bus to Darjeeling
       It said on the door
       "Please don't sit on the floor"
   So he carefully sat on the ceiling
```

```yaml
data: >
   Wrapped text
   will be folded
   into a single
   paragraph

   Blank lines denote
   paragraph breaks
```

```yaml
--- # The Smiths
- {name: John Smith, age: 33}
- name: Mary Smith
  age: 27
- [name, age]: [Rae Smith, 4]   # sequences as keys are supported
--- # People, by gender
men: [John Smith, Bill Jones]
women:
  - Mary Smith
  - Susan Williams
```

## Advanced components

Here are some advanced components in YAML:

```yaml
--- # Sequencer protocols for Laser eye surgery
- step:  &id001                  # defines anchor label &id001
    instrument:      Lasik 2000
    pulseEnergy:     5.4
    pulseDuration:   12
    repetition:      1000
    spotSize:        1mm

- step: &id002
    instrument:      Lasik 2000
    pulseEnergy:     5.0
    pulseDuration:   10
    repetition:      500
    spotSize:        2mm
- step: *id001                   # refers to the first step (with anchor &id001)
- step: *id002                   # refers to the second step
- step: *id002
```

```yaml
---
a: 123                     # an integer
b: "123"                   # a string, disambiguated by quotes
c: 123.0                   # a float
d: !!float 123             # also a float via explicit data type prefixed by (!!)
e: !!str 123               # a string, disambiguated by explicit type
f: !!str Yes               # a string via explicit type
g: Yes                     # a boolean True (yaml1.1), string "Yes" (yaml1.2)
h: Yes we have No bananas  # a string, "Yes" and "No" disambiguated by context.
```

```yaml
---
picture: !!binary |
  R0lGODdhDQAIAIAAAAAAANn
  Z2SwAAAAADQAIAAACF4SDGQ
  ar3xxbJ9p0qa7R0YxwzaFME
  1IAADs=
```

```yaml
---
myObject: !myClass { name: Joe, age: 15 }
```

## Other examples

Here is a data hierarchy in YAML.

```yaml
---
receipt:     Oz-Ware Purchase Invoice
date:        2012-08-06
customer:
    first_name:   Dorothy
    family_name:  Gale

items:
    - part_no:   A4786
      descrip:   Water Bucket (Filled)
      price:     1.47
      quantity:  4

    - part_no:   E1628
      descrip:   High Heeled "Ruby" Slippers
      size:      8
      price:     133.7
      quantity:  1

bill-to:  &id001
    street: |
            123 Tornado Alley
            Suite 16
    city:   East Centerville
    state:  KS

ship-to:  *id001

specialDelivery:  >
    Follow the Yellow Brick
    Road to the Emerald City.
    Pay no attention to the
    man behind the curtain.
...
```

Here is indented delimiting in YAML:

```yaml
---
example: >
        HTML goes into YAML without modification
message: |

        <blockquote style="font: italic 1em serif">
        <p>"Three is always greater than two,
           even for large values of two"</p>
        <p>--Author Unknown</p>
        </blockquote>
date: 2007-06-01
```

[Source: Wikipedia](https://en.wikipedia.org/wiki/YAML)

<!-- [Source: RosettaCode](http://rosettacode.org/wiki/Hello_world/Text#Swift) !-->

## Compatibility on GitHub

GitHub doesn't show YAML usage by default in the color coded language list, but you can force the GitHub linguist to recognize the language with this script:

```gitattributes
# YAML
*.yml linguist-detectable=true
*.yml linguist-documentation=false
```

Version B:

```gitattributes
# YAML
*.yaml linguist-detectable=true
*.yaml linguist-documentation=false
```

Here is an expanded version:

```gitattributes
# YAML
*.yml linguist-detectable=true
*.yml linguist-documentation=false
*.yaml linguist-detectable=true
*.yaml linguist-documentation=false
```

## Use by Apple

I cannot determine the use of YAML by Apple.

## Use by WacOS

WacOS uses YAML for certain functions, currently including:

1. Git data (such as labels, tags, and topics)

2. Sponsor info

3. Jekyll site configuration

4. Issue templates (coming soon)

_This article on programming languages is a stub. You can help by expanding it!_

***

## Sources

[Source: Wikipedia - YAML](https://en.wikipedia.org/wiki/YAML/)

More sources needed, Wikipedia cannot be the only source, as Wikipedia isn't really something to use as a source. More credible sources are needed.

***

## Article info

**Written on:** `2021, Tuesday, September 14th at 3:02 pm)`

**Last revised on:** `2021, Tuesday September 14th at 3:02 pm)`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `3 (2021, Sunday September 19th at 3:02 pm)`

***
