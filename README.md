# Klogg

[![GitHub license](https://img.shields.io/github/license/variar/klogg.svg?style=flat)](https://github.com/variar/klogg/blob/master/COPYING)
[![C++](https://img.shields.io/github/languages/top/variar/klogg?style=flat)]()
[![GitHub contributors](https://img.shields.io/github/contributors/variar/klogg.svg?style=flat)](https://github.com/variar/klogg/graphs/contributors/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](http://makeapullrequest.com)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/f6db6ef0be3a4a5abff94111a5291c45)](https://www.codacy.com/manual/variar/klogg?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=variar/klogg&amp;utm_campaign=Badge_Grade)


[![Github all releases](https://img.shields.io/github/downloads/variar/klogg/total?style=flat)](https://github.com/variar/klogg/releases/)
[ ![Github](https://img.shields.io/github/v/release/variar/klogg?style=flat&label=Stable%20release)](https://github.com/variar/klogg/releases/tag/v20.12)

[![Packaging status](https://repology.org/badge/vertical-allrepos/klogg.svg)](https://repology.org/project/klogg/versions)

Check [GitHub releases](https://github.com/variar/klogg/releases/latest) for Windows installers and Linux/Mac packages.

Development status

[![Next milestone](https://img.shields.io/github/milestones/progress-percent/variar/klogg/8?style=flat)](https://github.com/variar/klogg/milestone/8)
[![Ready for testing](https://img.shields.io/github/issues-raw/variar/klogg/status:%20ready%20for%20testing?color=green&label=issues%20ready%20for%20testing&style=flat)](https://github.com/variar/klogg/issues?q=is%3Aopen+is%3Aissue+label%3A%22status%3A+ready+for+testing%22)
[![GitHub commits](https://img.shields.io/github/commits-since/variar/klogg/v20.12.svg?style=flat)](https://github.com/variar/klogg/commits/)
[![CI Build and Release](https://github.com/variar/klogg/actions/workflows/ci-build.yml/badge.svg)](https://github.com/variar/klogg/actions/workflows/ci-build.yml)

[![Chat on Discord](https://img.shields.io/discord/838452586944266260?label=Discord&style=flat)](https://img.shields.io/discord/838452586944266260?label=Discord&style=flat)

## Overview

Klogg is a multi-platform GUI application that helps browse and search
through long and complex log files. It is designed with programmers and
system administrators in mind and can be seen as a graphical, interactive
combination of grep, less, and tail.

![Klogg main window](website/static/screenshots/mainwindow.png)

Please refer to the
[documentation](DOCUMENTATION.md)
page for how to use Klogg.

### Latest testing builds

| Windows | Linux | Mac |
| ------------- |------------- | ------------- |
| [continuous-win](https://github.com/variar/klogg/releases/tag/continuous-win) | [continuous-linux](https://github.com/variar/klogg/releases/tag/continuous-linux) | [continuous-osx](https://github.com/variar/klogg/releases/tag/continuous-osx) |

## Table of Contents

1. [About the Project](#about-the-project)
1. [Installation](#installation)
1. [Building](#building)
1. [How to Get Help](#how-to-get-help)
1. [Contributing](#contributing)
1. [License](#license)
1. [Authors](#authors)

## About the Project

Klogg started as a fork of [glogg](https://github.com/nickbnf/glogg) - the fast, smart log explorer in 2016.

Since then it has evolved from fixing small annoying bugs to rewriting core components to
make it faster and smarter that predecessor.

Development of klogg is driven by features my colleagues and I need
to stay productive as well as feature requests from users on Github and in glogg mailing list.

Latest news about klogg development can be found at https://klogg.filimonov.dev.

### Comparing with glogg

Klogg has all best features of glogg:

* Runs on Unix-like systems, Windows and Mac thanks to Qt5
* Is fast and reads the file directly from disk, without loading it into memory
* Can operate on huge text files (10+ Gb is not a problem)
* Search results are displayed separately from original file
* Supports Perl-compatible regular expressions
* Colorizes the log and search results
* Displays a context view of where in the log the lines of interest are
* Watches for file changes on disk and reloads it (kind of like tail)
* Is open source, released under the GPL

And on top of that klogg:

* Is heavily optimized using multithreading and SIMD
* Supports many common text encodings
* Detects file encoding automatically using [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/) library (supports utf8, utf16, cp1251 and more) 
* Can limit search operations to some part of huge file
* Allows to conifugure several highlighters sets and switch between them
* Has a scratchpad window for taking notes and doing basic data transformations
* Provides lots of small features that make life easier (closing tabs, copying file paths, favorite files menu, etc.)

Here is a small demo showing how much faster klogg is (searching in ~1Gb file stored on tmpfs):

https://user-images.githubusercontent.com/1620716/117588567-bea39100-b12c-11eb-990a-90a667bcaeaa.mp4

List of glogg issues that have been fixed/implemented in klogg:

* [glogg on a Mac doesn't automatically open associated files](https://github.com/nickbnf/glogg/issues/283)
* [regex error](https://github.com/nickbnf/glogg/issues/279)
* [Feature Request: Dark mode](https://github.com/nickbnf/glogg/issues/274)
* [Incremental search is killing performance](https://github.com/nickbnf/glogg/issues/263)
* [Fix Filter Dialog: Color information get lost](https://github.com/nickbnf/glogg/issues/262)
* [Duplicate search results](https://github.com/nickbnf/glogg/issues/247)
* [Feature Request: Loadable filter-files](https://github.com/nickbnf/glogg/issues/234)
* [Feature request - multi-threaded indexing and search](https://github.com/nickbnf/glogg/issues/227)
* [Feature Request: Add proper color picker](https://github.com/nickbnf/glogg/issues/204)
* [Gets confused when the log is turned over](https://github.com/nickbnf/glogg/issues/203)
* [Crashed on search in log files with 5 million lines](https://github.com/nickbnf/glogg/issues/200)
* [Feature Request: Allow glogg to stay in tray when minimized](https://github.com/nickbnf/glogg/issues/199)
* [Minor bug: Consolas Font sizes not working](https://github.com/nickbnf/glogg/issues/185)
* [Feature Request: keyboard shortcut for mark line](https://github.com/nickbnf/glogg/issues/179)
* [X on the right of search does not work](https://github.com/nickbnf/glogg/issues/173)
* [feature Request - Save my marked items](https://github.com/nickbnf/glogg/issues/168)
* [After checking "Ignore case", current filter's settings are applied to next filter selected](https://github.com/nickbnf/glogg/issues/160)
* [Keyboard command "jump to the line number" does not work](https://github.com/nickbnf/glogg/issues/157)
* [Searching while index is being built causes results to be missed](https://github.com/nickbnf/glogg/issues/150)
* [feature request - favorites](https://github.com/nickbnf/glogg/issues/147)
* [Feature request: Limit searches to section of file](https://github.com/nickbnf/glogg/issues/141)
* [File not followed](https://github.com/nickbnf/glogg/issues/135)
* [Feature Request: Support reading gzipped files](https://github.com/nickbnf/glogg/issues/122)
* [follow file 'f' short cut doesn't work](https://github.com/nickbnf/glogg/issues/118)
* [Glogg displaying old log file contents even after file deletion](https://github.com/nickbnf/glogg/issues/112)
* [I wish to add clear log menu option](https://github.com/nickbnf/glogg/issues/111)
* [Windows: Handling git better, not keep files open](https://github.com/nickbnf/glogg/issues/109)
* [allow for glogg (and settings) to be used from usb](https://github.com/nickbnf/glogg/issues/101)
* [Max length of search strings](https://github.com/nickbnf/glogg/issues/98)
* [Deadlock: Indexing lines...(99%)](https://github.com/nickbnf/glogg/issues/86)
* [Support presets for search expressions](https://github.com/nickbnf/glogg/issues/73)
* [Rename "Filters" to "Highlighters"](https://github.com/nickbnf/glogg/issues/71)
* [Maintain approx position in filtered window when changing filter regexp](https://github.com/nickbnf/glogg/issues/61)
* [Feature Request:: Read glogg.ini from EXE Path if present for Portable Mode](https://github.com/nickbnf/glogg/issues/50)
* [Generate a "Portable App" version for Windows in addition to the installer](https://github.com/nickbnf/glogg/issues/19)

**[Back to top](#table-of-contents)**

## Installation

This project uses [Calendar Versioning](https://calver.org/). For a list of available versions, see the [repository tag list](https://github.com/variar/klogg/tags).

### Current stable release builds

Current release is 20.12. Binaries for all platforms can be downloaded from GitHub releases.

[ ![Release](https://img.shields.io/github/v/release/variar/klogg?style=flat)](https://github.com/variar/klogg/releases/tag/v20.12)

#### Windows
Windows installer is also available from Chocolatey:

[ ![Chocolatey](https://img.shields.io/chocolatey/v/klogg?style=flat)](https://chocolatey.org/packages/klogg)

#### Mac OS
Package for Mac can be installed from Homebrew

[ ![homebrew cask](https://img.shields.io/homebrew/cask/v/klogg?style=flat)](https://formulae.brew.sh/cask/klogg)

#### Linux
It is recommended to use klogg package from distribution-specific [repositories](https://repology.org/project/klogg/versions).

Generic packages are available from klogg DEB and RPM repositories hosted at JFrog Artifactory.
They are built to run on Ubuntu 16.04 and Centos 8 (x86-64 only).

For DEB add klogg artifactory repository, then install from apt:
```
echo deb [trusted=yes] https://favpackage.jfrog.io/artifactory/klogg_deb stable utils | sudo tee -a /etc/apt/sources.list
sudo apt-get update
sudo apt-get install klogg
```

For RPM create klogg repo file in `/etc/yum.repos.d/klogg-rpm.repo` with this content:
```
[Klogg]
name=Klogg
baseurl=https://favpackage.jfrog.io/artifactory/klogg_rpm/
enabled=1
gpgcheck=0
```

Then install using yum
```
sudo yum update
sudo yum install klogg
```

### Tesing builds

![CI Build and Release](https://github.com/variar/klogg/workflows/CI%20Build%20and%20Release/badge.svg)

| Windows | Linux | Mac |
| ------------- |------------- | ------------- |
| [continuous-win](https://github.com/variar/klogg/releases/tag/continuous-win) | [continuous-linux](https://github.com/variar/klogg/releases/tag/continuous-linux) | [continuous-osx](https://github.com/variar/klogg/releases/tag/continuous-osx) |

**[Back to top](#table-of-contents)**

## Building

Please review
[BUILD.md](BUILD.md)
for how to setup Klogg on your local machine for development and testing purposes.

## How to Get Help

First, please refer to the
[documentation](DOCUMENTATION.md)
page.

You can open issues using [klogg issues page](https://github.com/variar/klogg/issues)
or post questions to glogg development [mailing list](http://groups.google.co.uk/group/glogg-devel).

## Contributing

We encourage public contributions! Please review [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct and development process.

## License

This project is licensed under the GPLv3 or later - see [COPYING](COPYING) file for details.

## Authors

* **[Anton Filimonov](https://github.com/variar)**
* *Initial work* - **[Nicolas Bonnefon](https://github.com/nickbnf)**

See also the list of [contributors](https://klogg.filimonov.dev/docs/getting_involved/#contributors) who participated in this project.

**[Back to top](#table-of-contents)**
