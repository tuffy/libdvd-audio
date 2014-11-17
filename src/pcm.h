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

#include <stdint.h>
#include "array.h"
#include "bitstream.h"
#include "stream_parameters.h"

struct PCMDecoder_s;

typedef struct PCMDecoder_s PCMDecoder;

PCMDecoder*
dvda_open_pcmdecoder(unsigned bits_per_sample, unsigned channel_count);

void
dvda_close_pcmdecoder(PCMDecoder* decoder);

/*decodes the PCM stream parameters from the start of the packet*/
void
dvda_pcmdecoder_decode_params(BitstreamReader *packet_reader,
                              struct stream_parameters* parameters);

/*given a packet reader substream
  (not including the stream parameters or second padding)
  decodes as many samples as possible to samples
  and returns the number of PCM frames decoded*/
unsigned
dvda_pcmdecoder_decode_packet(PCMDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples);
