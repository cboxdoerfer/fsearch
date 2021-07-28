![Build Status](https://github.com/cboxdoerfer/fsearch/actions/workflows/build_test.yml/badge.svg)
[![Translation status](https://hosted.weblate.org/widgets/fsearch/-/svg-badge.svg)](https://hosted.weblate.org/engage/fsearch/?utm_source=widget)


FSearch is a fast file search utility, inspired by Everything Search Engine. It's written in C and based on GTK3.

**Note: The application is still in beta stage**

* For bug reports and feature requests please use the issue tracker: https://github.com/cboxdoerfer/fsearch/issues
* For everything else related to FSearch you can talk to me on Matrix: https://matrix.to/#/#fsearch:matrix.org

![](https://user-images.githubusercontent.com/6108388/94472642-51f60a80-01cb-11eb-9b8c-e38fe2c02e43.png)
![](https://user-images.githubusercontent.com/6108388/94607184-dd3ed100-029b-11eb-86db-121e7984ca92.png)


## Features
- Instant (as you type) results
- Wildcard support
- RegEx support
- Filter support (only search for files, folders or everything)
- Include and exclude specific folders to be indexed
- Ability to exclude certain files/folders from index using wildcard expressions
- Fast sort by filename, path, size or modification time
- Customizable interface (e.g. switch between traditional UI with menubar and client-side decorations)

## Requirements
- GTK 3.18
- GLib 2.44
- glibc 2.19 or musl 1.1.15 (other C standard libraries might work too, those are just the ones I verified)
- PCRE (libpcre)
- ICU 3.8

## Download

#### Arch Linux (AUR)

https://aur.archlinux.org/packages/fsearch-git/

#### Fedora/RHEL/CentOS

[![Copr build status](https://copr.fedorainfracloud.org/coprs/cboxdoerfer/fsearch_nightly/package/fsearch/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/cboxdoerfer/fsearch_nightly/package/fsearch/)

https://copr.fedorainfracloud.org/coprs/cboxdoerfer/fsearch_nightly/

##### copr Installation

```
sudo dnf copr enable cboxdoerfer/fsearch_nightly
sudo dnf install fsearch
````

#### NixOS (unofficial)
https://search.nixos.org/packages?channel=unstable&show=fsearch&query=fsearch
#### openSUSE (unofficial)
https://software.opensuse.org/download.html?project=home%3AAsterPhoenix13&package=fsearch
#### Ubuntu
##### Daily Development Builds PPA
https://launchpad.net/~christian-boxdoerfer/+archive/ubuntu/fsearch-daily

##### PPA Installation

```
sudo add-apt-repository ppa:christian-boxdoerfer/fsearch-daily
sudo apt-get update
sudo apt install fsearch-trunk
```
 
## Roadmap
https://github.com/cboxdoerfer/fsearch/wiki/Roadmap

## Build Instructions
https://github.com/cboxdoerfer/fsearch/wiki/Build-instructions

## Localization
The localization of FSearch is managed with Weblate.

https://hosted.weblate.org/projects/fsearch/

If you want to contribute translations please submit them there, instead of opening pull requets on Github. Instructions can be found here: 
https://docs.weblate.org/en/latest/user/basic.html

And of course: Thank you for taking the time to translate FSearch!

## Why yet another search utility?
Performance. On Windows I really like to use Everything Search Engine. It provides instant results as you type for all your files and lots of useful features (regex, filters, bookmarks, ...). On Linux however I couldn't find anything that's even remotely as fast and powerful.

Before I started working on FSearch I took a look at all the existing solutions I found (MATE Search Tool (formerly GNOME Search Tool), Recoll, Krusader (locate based search), SpaceFM File Search, Nautilus, ANGRYsearch, Catfish, ...) to find out whether it makes sense to improve those, instead of building a completely new application. But unfortunately none of those met my requirements:
- standalone application (not part of a file manager)
- written in a language with C like performance
- no dependencies to any specific desktop environment
- Qt5 or GTK+3 based
- small memory usage (both hard drive and RAM)
- target audience: advanced users

## Looking for a command line interface?
I highly recommend [fzf](https://github.com/junegunn/fzf) or the obvious tools: find and (m)locate

## Why GTK3 and not Qt5?
I like both of them. And in fact my long term goal is to provide console, GTK3 and Qt5 interfaces, or at least make it possible for others to build those by splitting the search and database functionality into a core library. But for the time being it's only GTK3 because I tend to like C more than C++ and I'm more familiar with GTK development.

## Questions?

Email: christian.boxdoerfer[AT]posteo.de
