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

#ifndef __LIBDVDAUDIO_PACKET_H__
#define __LIBDVDAUDIO_PACKET_H__

#include "aob.h"
#include "bitstream.h"

struct Packet_Reader_s;

typedef struct Packet_Reader_s Packet_Reader;

/*given an AOB_Reader, opens a packet reader
  which pulls apart the stream of sectors into individual packets*/
Packet_Reader*
packet_reader_open(AOB_Reader *aob_reader);

/*closes the packet reader but *not* the enclosed AOB reader*/
void
packet_reader_free(Packet_Reader *packet_reader);

/*closes the encloses AOB reader and then frees the packet reader*/
void
packet_reader_close(Packet_Reader *packet_reader);

/*returns the next packet from a stream as a BitstreamReader
  of its data (not including the 48 bit header)
  along with the stream ID
  and sector number

  returns NULL if there are no more packets to read*/
BitstreamReader*
packet_reader_next_packet(Packet_Reader *packet_reader,
                          unsigned *stream_id,
                          unsigned *sector);

/*returns the next audio packet from the stream
  or NULL if there are no more packets to read*/
BitstreamReader*
packet_reader_next_audio_packet(Packet_Reader *packet_reader,
                                unsigned *sector);

#endif
