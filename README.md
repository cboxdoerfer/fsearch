FSearch is a fast file search utility for GNU/Linux operating systems, inspired by Everything Search Engine. It's written in C and based on GTK+3.

**Note: The application is in an early development stage and still considered pre-alpha**

![](https://i.imgur.com/WQbeSZJ.png)

## Features
- Instant (as you type) results
- RegEx support
- Filter support (only search for files, folders or everything)
- Fast sort by filename, path, size or modification time
- Customizable interface

## Requirements
- GTK+ 3.12 or newer
- PCRE (libpcre)
 
## Roadmap
https://github.com/cboxdoerfer/fsearch/wiki/Roadmap

## Building
https://github.com/cboxdoerfer/fsearch/wiki/Build-instructions

## Why yet another search utility?
Easy one, performance. On Windows I really like to use Everything Search Engine. It provides instant results as you type for all your files and lots of useful features (regex, filters, bookmarks, ...). On Linux however I couldn't find anything that's even remotely as fast and powerful.

Before I started working on FSearch I took a look at all the existing solutions I found (MATE Search Tool (formerly GNOME Search Tool), Recoll, Krusader (locate based search), SpaceFM File Search, Nautilus, ANGRYsearch, Catfish, ...) to find out whether it makes sense to improve those, instead of building a completely new application. But unfortunately none of those met my requirements:
- standalone application (not part of a file manager)
- written in a language with C like performance
- no dependencies to any specific desktop environment
- Qt5 or GTK+3 based
- small memory usage (both hard drive and RAM)
- target audience: advanced users

## Why GTK+3 and not Qt5?
I like both of them. And in fact my long term goal is to provide console, GTK+3 and Qt5 interfaces, or at least make it possible for others to build those by splitting the search and database functionality into a core library. But for the time being it's only GTK+3 because I tend to like C more than C++, I'm more familiar with GTK+ development, I almost exclusively run GTK+ applications and I really like some of its new widgets (e.g. Popovers).

## Download
### AUR
https://aur.archlinux.org/packages/fsearch-git/
