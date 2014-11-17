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

#ifndef __LIBDVDAUDIO_AUDIO_TS_H__
#define __LIBDVDAUDIO_AUDIO_TS_H__

/*a case-insensitive version of strcmp*/
int
strcmp_insensitive(const char *s1, const char *s2);

/*given a path to the AUDIO_TS directory
  and a filename to search for
  returns the full path to the file
  or NULL if the file is not found
  the path must be freed later once no longer needed

   filenames are compared case-insensitively*/
char*
find_audio_ts_file(const char* audio_ts_path, const char* filename);

#endif
