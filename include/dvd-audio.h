/********************************************************
 DVD-A Library, a module for reading DVD-Audio discs
 Copyright (C) 2014  Brian Langenberger

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************/

#include <inttypes.h>

#define LIBDVDAUDIO_MAJOR_VERSION 1
#define LIBDVDAUDIO_MINOR_VERSION 0
#define LIBDVDAUDIO_RELEASE_VERSION 0

#define TO_STR(x) #x
#define VERSION_STR(x) TO_STR(x)
#define LIBDVDAUDIO_MKVERSION(major, minor, release, extra) \
VERSION_STR(major) "." VERSION_STR(minor) "." VERSION_STR(release) extra

#define LIBDVDAUDIO_VERSION_STRING \
LIBDVDAUDIO_MKVERSION(LIBDVDAUDIO_MAJOR_VERSION, \
                      LIBDVDAUDIO_MINOR_VERSION, \
                      LIBDVDAUDIO_RELEASE_VERSION, \
                      "")

#define PTS_PER_SECOND 90000

struct DVDA_s;
struct DVDA_Titleset_s;
struct DVDA_Title_s;
struct DVDA_Track_s;
struct DVDA_Index_s;
struct DVDA_Track_Reader_s;

typedef struct DVDA_s DVDA;
typedef struct DVDA_Titleset_s DVDA_Titleset;
typedef struct DVDA_Title_s DVDA_Title;
typedef struct DVDA_Track_s DVDA_Track;
typedef struct DVDA_Index_s DVDA_Index;
typedef struct DVDA_Track_Reader_s DVDA_Track_Reader;

typedef enum {DVDA_PCM, DVDA_MLP} dvda_codec_t;

/*given a path to the disc's AUDIO_TS directory
  and a device (such as "/dev/cdrom") - which may be NULL,
  returns a DVDA or NULL if AUDIO_TS.IFO is missing or invalid

  the DVDA should be closed with dvda_close() when no longer needed*/
DVDA*
dvda_open(const char *audio_ts_path, const char *device);

/*closes the DVDA and deallocates any space it may have allocated*/
void
dvda_close(DVDA *dvda);

/*returns the number of title sets in the DVD-A*/
unsigned
dvda_titleset_count(const DVDA *dvda);

/*given a title set number (starting from 1)
  returns a DVDA_Titleset or NULL if ATS_XX_0.IFO is missing or invalid

  the DVDA_Titleset should be closed with dvda_close_titleset()
  when no longer needed*/
DVDA_Titleset*
dvda_open_titleset(DVDA* dvda, unsigned titleset);

void
dvda_close_titleset(DVDA_Titleset* titleset);

/*returns the titleset's number, starting from 1*/
unsigned
dvda_titleset_number(const DVDA_Titleset* titleset);

/*returns the number of titles in the title set*/
unsigned
dvda_title_count(const DVDA_Titleset* titleset);

/*given a title number (starting from 1)
  returns a DVDA_Title or NULL if the title is not found

  the DVDA_Title should be closed with dvda_close_title()
  when no longer needed*/
DVDA_Title*
dvda_open_title(DVDA_Titleset* titleset, unsigned title);

void
dvda_close_title(DVDA_Title* title);

/*returns the title's number, starting from 1*/
unsigned
dvda_title_number(const DVDA_Title* title);

/*returns the number of tracks in the title*/
unsigned
dvda_track_count(const DVDA_Title* title);

/*returns the total length of the title in PTS ticks

  there are 90000 PTS ticks per second*/
unsigned
dvda_title_pts_length(const DVDA_Title* title);

/*given a track number (starting from 1)
  returns a DVDA_Track* or NULL if the track is not found

  the DVDA_Track should be closed with dvda_close_track()
  when no longer needed*/
DVDA_Track*
dvda_open_track(DVDA_Title* title, unsigned track);

void
dvda_close_track(DVDA_Track* track);

/*returns the track's number, starting from 1*/
unsigned
dvda_track_number(const DVDA_Track* track);

/*returns the index of the track in PTS ticks

  there are 90000 PTS ticks per second*/
unsigned
dvda_track_pts_index(const DVDA_Track* track);

/*returns the total length of the track in PTS ticks

  there are 90000 PTS ticks per second*/
unsigned
dvda_track_pts_length(const DVDA_Track* track);

/*returns the track's first sector

  note that it may not start at the very beginning of the sector*/
unsigned
dvda_track_first_sector(const DVDA_Track* track);

/*returns the track's last sector

  note that it may not end at the very end of the sector*/
unsigned
dvda_track_last_sector(const DVDA_Track* track);

/*given a DVDA_Track object, returns a DVDA_Track_Reader
  or NULL if some error occurs opening the track for reading

  the DVDA_Track_Reader should be closed with dvda_close_track_reader()
  when no longer needed*/
DVDA_Track_Reader*
dvda_open_track_reader(const DVDA_Track* track);

void
dvda_close_track_reader(DVDA_Track_Reader* reader);

/*returns the track's codec, such as PCM or MLP*/
dvda_codec_t
dvda_codec(const DVDA_Track_Reader* reader);

/*returns the track's bits-per-sample (16 or 24)*/
unsigned
dvda_bits_per_sample(const DVDA_Track_Reader* reader);

/*returns the track's sample rate in Hz*/
unsigned
dvda_sample_rate(const DVDA_Track_Reader* reader);

/*returns the track's number of channels*/
unsigned
dvda_channel_count(const DVDA_Track_Reader* reader);

/*returns a 32-bit RIFF WAVE channel mask*/
unsigned
dvda_riff_wave_channel_mask(const DVDA_Track_Reader *reader);

/*given a buffer with at least:

  dvda_channel_count(reader) * pcm_frames

  integers, populates that buffer with as many samples as possible
  interleaved on a per-channel basis:

  {left[0], right[0], left[1], right[1], ..., left[n], right[n]}

  in RIFF WAVE channel order

  returns the number of PCM frames actually read
  which may be less than requested at the end of the stream
*/
unsigned
dvda_read(DVDA_Track_Reader* reader,
          unsigned pcm_frames,
          int buffer[]);
