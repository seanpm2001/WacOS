
***

# Project language file number 13

## For the WacOS operating system project

The 13th project language file for this project (WacOS)  is Markdown, as this project uses Markdown significantly in documentation, and I am now including all languages as project language files.

This is a rare instance in which I am using Markdown as a project language file, as I learned how to make the GitHub linguist recognize Markdown as a language. I found this out when snooping around on [Apple/Swift Evolution](https://github.com/apple/swift-evolution/blob/main/.gitattributes) if you want to do this yourself, simply add this to your `.gitattributes` file (if you haven't created one yet, make it now, do not add anything else to the file name but `.gitattributes`)

```gitattributes
*.md linguist-detectable=true
*.md linguist-documentation=false
```

Extended version:

```gitattributes
# Markdown
*.md linguist-detectable=true
*.md linguist-documentation=false
*.mkd linguist-detectable=true
*.mkd linguist-documentation=false
*.markdown linguist-detectable=true
*.markdown linguist-documentation=false
```

## File info

File type: `Markdown document (*.md)`

File version: `1 (Saturday, 2021 September 25th at 7:17 pm)`

Line count (including blank lines and compiler line): `38`

***
