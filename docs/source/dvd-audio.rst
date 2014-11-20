.. default-domain:: c

DVD-Audio Library
=================

DVD-Audio discs were a short-lived format for distributing
lossless, high-definition audio on DVD media.
This is a library designed to extract the contents of those discs
as simply and painlessly as possible using either the included
example tools or tools of your own that are linked against this library.
Such discs can be identified by mounting them and looking for contents
in the ``AUDIO_TS`` directory.
They typically contain one or more ``.AOB`` files and several ``.IFO`` files.

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

or, set the library flags manually:

::

    cc -o myprogram myprogram.c -ldvd-audio -lm

Note that the math library is also required, which should come standard.

Reference
=========

All of the following types and functions are defined in the C header:

::

    #include <dvd-audio.h>

Macros
^^^^^^

.. macro:: LIBDVDAUDIO_MAJOR_VERSION

   the library's major version as an integer

.. macro:: LIBDVDAUDIO_MINOR_VERSION

   the library's minor version as an integer

.. macro:: LIBDVDAUDIO_RELEASE_VERSION

   the library's release version as an integer

.. macro:: LIBDVDAUDIO_VERSION_STRING

   the library's version as a string

.. macro:: PTS_PER_SECOND

   90000

Objects
^^^^^^^

   All of the structures used by this library are opaque
   and given ``typedefs`` to shorten their names.

.. type:: DVDA

   A structure that references the entire disc.

.. type:: DVDA_Titleset

   A structure that references a given title set on the disc.

.. type:: DVDA_Title

   A structure that references a given title in the title set.

.. type:: DVDA_Track

   A structure that references a given track in the title.

.. type:: DVDA_Track_Reader

   A file-like handle for reading data from a given track.

DVDA Functions
^^^^^^^^^^^^^^

.. function:: DVDA* dvda_open(const char *audio_ts_path, const char *device)

   Given a path to the disc's ``AUDIO_TS`` directory
   (such as ``"/media/cdrom/AUDIO_TS"``)
   and optional CD-ROM device where the disc has been mounted from
   (such as ``"/dev/cdrom"``),
   returns a :type:`DVDA` pointer or ``NULL`` if some error occurs
   opening the disc.

   The :type:`DVDA` must be freed with :func:`dvda_close` when
   no longer needed.

   The ``device`` argument is for performing decryption of the disc's
   contents.
   Encrypted discs contain a ``DVDAUDIO.MKB`` file in the ``AUDIO_TS``
   directory.
   If no ``DVDAUDIO.MKB`` is found or the ``device`` argument is ``NULL``,
   no decryption will be performed.

.. function:: void dvda_close(DVDA *dvda)

   Closes the :type:`DVDA` and deallocates any memory it may have.

.. function:: unsigned dvda_titleset_count(const DVDA *dvda)

   Returns the number of title sets on the disc.

Titleset Functions
^^^^^^^^^^^^^^^^^^

.. function:: DVDA_Titleset* dvda_open_titleset(DVDA *dvda, unsigned titleset)

   Given a title set number (starting from 1)
   returns a :type:`DVDA_Titleset` or ``NULL`` if
   the disc's ``ATS_XX_0.IFO`` file is missing or invalid.

   The :type:`DVDA_Titleset` should be closed with
   :func:`dvda_close_titleset` when no longer needed.

.. function:: void dvda_close_titleset(DVDA_Titleset *titleset)

   Closes the :type:`DVDA_Titleset` and deallocates any memory
   it may have.


.. function:: unsigned dvda_titleset_number(const DVDA_Titleset *titleset)

   Returns the title set's number.

.. function:: unsigned dvda_title_count(const DVDA_Titleset *titleset)

   Returns the number of titles in the title set.

Title Functions
^^^^^^^^^^^^^^^

.. function:: DVDA_Title* dvda_open_title(DVDA_Titleset *titleset, unsigned title)

   Given a title number (starting from 1)
   returns a :type:`DVDA_Title` or ``NULL`` if the title
   is not found in the title set.

   The :type:`DVDA_Title` should be closed with
   :func:`dvda_close_title` when no longer needed.

.. function:: void dvda_close_title(DVDA_Title *title)

   Closes the :type:`DVDA_Title` and deallocates any memory
   it may have.

.. function:: unsigned dvda_title_number(const DVDA_Title *title)

   Returns the title's number.

.. function:: unsigned dvda_track_count(const DVDA_Title *title)

   Returns the number of tracks in the title.

.. function:: unsigned dvda_title_pts_length(const DVDA_Title *title)

   Returns the length of title in PTS ticks.

Track Functions
^^^^^^^^^^^^^^^

.. function:: DVDA_Track* dvda_open_track(DVDA_title *Title, unsigned track)

   Given a track number (starting from 1)
   returns a :type:`DVDA_Track` or ``NULL`` if the track
   is not found in the title.

   The :type:`DVDA_Track` should be closed with
   :func:`dvda_close_track` when no longer needed.

.. function:: void dvda_close_track(DVDA_Track *track)

   Closes the :type:`DVDA_Track` and deallocates any memory
   it may have.

.. function:: unsigned dvda_track_number(const DVDA_Track *track)

   Returns the track's number.

.. function:: unsigned dvda_track_pts_index(const DVDA_Track *track)

   Returns the starting point of the track in the stream in PTS ticks.

.. function:: unsigned dvda_track_pts_length(const DVDA_Track *track)

   Returns the length of the track in PTS ticks.

.. function:: unsigned dvda_track_first_sector(const DVDA_Track *track)

   Returns the track's first sector in the stream of ``AOB`` files.

.. function:: unsigned dvda_track_last_sector(const DVDA_Track *track)

   Returns the track's last sector in the stream of ``AOB`` files.

Track Reader Functions
^^^^^^^^^^^^^^^^^^^^^^

.. function:: DVDA_Track_Reader* dvda_open_track_reader(DVDA_Track *track)

   Returns a :type:`DVDA_Track_Reader` for reading audio data
   from the given track, or ``NULL`` if some error occurs
   opening the track for reading.

   The :type:`DVDA_Track_Reader` should be closed with
   :func:`dvda_close_track_reader` when no longer needed.

.. function:: void dvda_close_track_reader(DVDA_Track_Reader *reader)

   Closes the :type:`DVDA_Track_Reader` along with any file handles
   or memory it may have.

.. function:: dvda_codec_t dvda_codec(DVDA_Track_Reader *reader)

   Returns the reader's codec, such as ``DVDA_PCM`` or ``DVDA_MLP``.
   This is purely for informative purposes.

.. function:: unsigned dvda_bits_per_sample(DVDA_Track_Reader *reader)

   Returns the reader's bits-per-sample - either 24 or 16.

.. function:: unsigned dvda_sample_rate(DVDA_Track_Reader *reader)

   Returns the reader's sample rate, in Hz.

.. function:: unsigned dvda_channel_count(DVDA_Track_Reader *reader)

   Returns the reader's number of channels - often 2 or 6.

.. function:: unsigned dvda_riff_wave_channel_mask(DVDA_Track_Reader *reader)

   Returns the reader's channel mask as a 32-bit value.
   Each set bit indicates the presence of the given channel:

   ============ =========
   channel      bit
   ============ =========
   front left   ``0x001``
   front right  ``0x002``
   front center ``0x004``
   LFE          ``0x008``
   back left    ``0x010``
   back right   ``0x020``
   back center  ``0x100``
   ============ =========

.. function:: uint64_t dvda_total_pcm_frames(DVDA_Track_Reader *reader)

   Returns the total number of PCM frames in the given reader.
   A PCM frame is a single set of samples across all the stream's channels.
   That is, the total length of the stream in seconds can be calculated by:

::

   dvda_total_pcm_frames(reader) / dvda_sample_rate(reader)

..

   but the the total length of the stream in bytes can be calculated by:

::

   dvda_total_pcm_frames(reader) * dvda_channel_count(reader) * (dvda_bits_per_sample(reader) / 8)

.. function:: unsigned dvda_read(DVDA_Track_Reader *reader, unsigned pcm_frames, int buffer[])

   Given a number of PCM frames and a buffer which contains at least:

::

   dvda_channel_count(reader) * pcm_frames

..

   integers, populates that buffer with as many signed integer samples
   as possible and interleaved on a per-channel basis, like:

::

   {left[0], right[0], left[1], right[1], ..., left[n], right[n]}

..

   in RIFF WAVE channel order.

   Returns the number of PCM frames actually read,
   which may be less than the number requested at the end of the stream.
