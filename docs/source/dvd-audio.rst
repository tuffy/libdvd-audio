.. default-domain:: c

DVD-Audio Library
=================

DVD-Audio discs were a short-lived format for distributing
lossless, high-definition audio on DVD media.
This is a library designed to extract the contents of those discs
as simply and painlessly as possible using either the included
example tools or tools of your own that are linked against this library.
Such discs can be identified by mounting them and looking for contents
in the ``AUDIO_TS`` directory, typically one or more ``.AOB`` files
and several ``.IFO`` files.

This library does *not* work on DVD-Video discs, at all;
the contents of ``VIDEO_TS`` are completely unsupported.

DVD-Audio Disc Layout
=====================

Unlike Compact Disc audio which is a single stream of uniform data,
a single DVD-Audio disc may contain multiple title sets,
where each title set may contain multiple titles,
and each title may contain multiple tracks.
The ``AUDIO_TS.IFO`` file contains information about all the title sets
(though in practice, we're only ever interested in the first title set).
The ``ATS_01_0.IFO`` file contains information about all the titles
in title set ``01`` and includes track length and offset data.
Finally, the ``ATS_01_X.AOB`` files contain all the audio data itself.
These ``.AOB`` files are actually a continuous stream of
2048 byte sectors that are typically broken up into
files about 1 gigabyte each.

Audio data is stored either as uncompressed PCM or in
Meridian Lossless Packing format.
The stream attributes such as sample rate and bits-per-sample
often differ from title to title on the same disc
(title 1 may contain a 5.1 channel stream while title 2 contains
a 2 channel stream, for instance).
More rarely, tracks within the same title may have different stream
attributes.

Knowing these basics makes it easier to understand why this library
is organized the way it is.

Installation
============

This library includes all the dependencies it needs.
To install it, edit the ``Makefile`` if desired
and update the directory where the static and dynamic libraries
will be installed (``LIB_DIR``), where its single ``dvd-audio.h``
include file will be installed (``INCLUDE_DIR``),
where the reference binaries will be install (``BIN_DIR``) and
where pkg-config's metadata will be installed (``PKG_CONFIG_DIR``).

Once satisfied, simply run ``make`` and ``make install``.

Linking
=======

The easiest way to link one's own program against this library
is to use pkg-config to indicate what all the flags should be, like:

::

    cc -o myprogram myprogram.c `pkg-config --cflags --libs libdvd-audio`

or, just use the standard flags:

::

    cc -o myprogram myprogram.c -ldvd-audio -lm

Note that the standard math library is also required.

Reference
=========

All of the following types and functions are defined in the C header:

::

    #include <dvd-audio.h>
