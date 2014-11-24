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

#ifndef __LIBDVDAUDIO_AOB_H__
#define __LIBDVDAUDIO_AOB_H__

#include <stdint.h>

struct AOB_Reader_s;

typedef struct AOB_Reader_s AOB_Reader;

/*given a full path to an AUDIO_TS directory
  cdrom device (or NULL)
  and title set number (starting from 1),
  returns an AOB_Reader or NULL if an error occured*/
AOB_Reader*
aob_reader_open(const char *audio_ts_path,
                const char *cdrom_device,
                unsigned titleset);

/*closes an opened reader*/
void
aob_reader_close(AOB_Reader *reader);

/*given a reader and sector buffer,
  reads exactly 2048 bytes to that buffer
  and returns 0 on success, 1 on failure*/
int
aob_reader_read(AOB_Reader *reader, uint8_t *sector_data);

/*seeks to the given sector number
  and returns 0 on success, 1 on failure*/
int
aob_reader_seek(AOB_Reader *reader, unsigned sector_number);

#endif
