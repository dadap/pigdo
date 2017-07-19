pigdo - Parallel Implementation of Jigsaw Download
==================================================

Introduction
------------

pigdo is a program for reconstructing files that have been distributed in jigdo
format. For more information about the original jigdo project, see:

http://atterer.org/jigdo/

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

The included makefile should work to build pigdo on most UNIX-like systems.
Pigdo has been tested on GNU/Linux, Mac OS X, and FreeBSD. The makefile only
works with GNU make on FreeBSD for now, but I'll either try to fix it so that it
works with both GNU and BSD make, or port the build to a meta-build system to
generate the appropriate makefile, and add feature tests for the dependencies
and for a few not entirely portable libc extensions that pigdo benefits from.

Running make with the default target should produce a single executable, "pigdo"
in the top-level directory. There is currently no makefile target to install
pigdo.

Usage
-----

Pigdo currently takes a single argument, a path to a .jigdo file. Pigdo will
infer the path to the .template file based on the template file path stored in
the .jigdo file, and resolve it relative to the .jigdo file's location. Pigdo
will then begin reconstructing the original target file, using the filename
stored in the .jigdo file, and save it relative to the .jigdo file's location.

In the near future, command line options will be added to allow finer-grained
control over the locations of the files and several runtime options.

Documentation
-------------

Currently, the only documentation for pigdo is this README file and the source
code. The source is commented in a Doxygen-compatible style, and documentation
may be generated from the source code using Doxygen by building the "docs"
target in the Makefile.

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
* Pigdo is intended to allow searching for matching files in local directories.
* Pigdo is intended to allow caching downloaded files locally, in addition to
  assembling them directly into the target output file.
* Pigdo is intended to support writing a log file while reconstruction is in
  progress, to allow interrupted downloads to be resumed later.
