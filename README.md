pigdo - Parallel Implementation of Jigsaw Download
==================================================

Introduction
------------

pigdo is a program for reconstructing files that have been distributed in jigdo
format. For more information about the original jigdo project, see:

[http://atterer.org/jigdo/](http://atterer.org/jigdo/)

The primary motivation behind pigdo is to provide a jigdo download client that
is more efficient than the existing "jigdo-lite" and "jigit" jigdo download
clients in space and in time:

* Pigdo is more space-efficient than the existing jigdo download clients because
  it writes downloaded data directly to the output file, rather than downloading
  them to intermediate files which are then assembled into the output file.
* This also makes pigdo more time-efficient, as it only needs to write the data
  once, rather than writing it once to save it to an intermediate file, then
  again to save it to the reassembled target file.
* Pigdo is also more time-efficient than the existing clients because it can
  download multiple files simultaneously (hence "parallel" in its name).
* This also makes pigdo more bandwidth-efficient as far as mirrors hosting the
  component files are concerned, because it supports downloading from multiple
  different mirrors, spreading the load across them. Some of the literature
  around jigdo seems to imply that being kinder to download mirrors is one of
  the advantages of downloading files via jigdo, yet there do not appear to be
  any existing jigdo clients that download files from more than one mirror at a
  time, which means that an individual download server is still responsible for
  servicing the entirety of a jigdo download, which doesn't seem much kinder
  than just downloading a single large file, except perhaps for the lightened
  storage burden of not having to keep a copy of it.

The name of the project is inspired by, but does not follow the same form as,
the pigz project (and to a lesser extent, pixz). It also makes the author think
of pigs with fancy hairstyles, which is a pretty cute thing to think about.

Building
--------

Pigdo has dependencies upon:

* libcurl, for fetching files over the network
* zlib and libbz2, for decompressing bzip2, zlib, and gzip streams
* POSIX Threads
* Doxygen, for generating source code documentation (optional)
* GNU Autotools (including the Autoconf Archive), if building from git

If any of these dependencies (apart from Autotools if you are building from a
tarball, or Doxygen) are missing, pigdo may still build successfully, but might
be limited in functionality to the point that it is unable to process .jigdo
files.

The standard GNU Autotools workflow will build and install pigdo from the source
tarball, i.e.:

1. ./configure
2. make
3. make install

The install step is optional: the pigdo executable may be run directly from the
source directory once it is built.

Note that the ./configure script is not checked into the git repository; the
standard autotools workflow for regenerating ./configure is needed.

Usage
-----

Pigdo takes, at a minimum, a single argument: a path to a .jigdo file. This may
be located on your filesystem, or it may be a URI to a remote location. In the
absence of any other arguments, pigdo will get the locations of the .template
file and the output file, and the locations to search for matching files from
the .jigdo file. These values can be supplemented or overridden using optional
command line arguments.

For more detail on the individual command line options, run pigdo without any
arguments to print a help message.

Documentation
-------------

Currently, the only documentation for pigdo is this README file and the source
code. The source is commented in a Doxygen-compatible style, and documentation
may be generated from the source code using Doxygen by running doxygen from the
source directory.

Copying
-------

You are free to use, study, and redistribute pigdo under the terms of version 2
of the GNU General Public License. See the "COPYING" file for the full text of
the GPLv2.

TODO
----

Pigdo is a work in progress, and while it is likely to work for many .jigdo
files, if not most .jigdo files, there are several known unimplemented features
and known bugs, and there are likely to be several unknown bugs as well.

Missing features include, but are not limited to:

* Several features documented as being part of the .jigdo file format are not
  handled correctly; for example, quoting and character escapes and comments,
  specifying multiple possible paths for resolving matched files in the .jigdo
  file, and handling local and remote paths in places where pigdo currently only
  supports remote and local paths, respectively.
* Some fields that are part of the .jigdo file format are ignored.
* Pigdo is intended to allow caching downloaded files locally, in addition to
  assembling them directly into the target output file.
