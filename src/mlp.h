/********************************************************
 DVD-A Library, a module for reading DVD-Audio discs
 Copyright (C) 2014-2015  Brian Langenberger

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

#include <stdint.h>
#include "array.h"
#include "bitstream.h"
#include "stream_parameters.h"

struct MLPDecoder_s;

typedef struct MLPDecoder_s MLPDecoder;

MLPDecoder*
dvda_open_mlpdecoder(const struct stream_parameters* parameters);

void
dvda_close_mlpdecoder(MLPDecoder* decoder);

/*given a packet reader substream
  (not including the header or pad 2 bytes)
  decodes as many samples as possible to samples
  and returns the number of PCM frames decoded*/
unsigned
dvda_mlpdecoder_decode_packet(MLPDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples);
