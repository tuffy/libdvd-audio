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

#include "pcm.h"
#include "bitstream.h"
#include <stdlib.h>

#define SECTOR_SIZE 2048

extern int
read_pack_header(BitstreamReader *sector_reader,
                 uint64_t *pts,
                 unsigned *SCR_extension,
                 unsigned *bitrate);

extern int
read_packet_header(BitstreamReader* sector_reader,
                   unsigned *stream_id,
                   unsigned *packet_length);

static int
SL16_char_to_int(unsigned char *s);

static int
SL24_char_to_int(unsigned char *s);

struct PCMDecoder_s {
    unsigned bps;
    int (*converter)(unsigned char *);
    unsigned channels;
    unsigned bytes_per_sample;
    unsigned chunk_size;
};

PCMDecoder*
dvda_open_pcmdecoder(unsigned bits_per_sample, unsigned channel_count)
{
    PCMDecoder* decoder = malloc(sizeof(PCMDecoder));

    if (bits_per_sample == 16) {
        decoder->bps = 0;
        decoder->converter = SL16_char_to_int;
    } else {
        decoder->bps = 1;
        decoder->converter = SL24_char_to_int;
    }

    decoder->channels = channel_count;

    decoder->bytes_per_sample = bits_per_sample / 8;

    decoder->chunk_size = decoder->bytes_per_sample * channel_count * 2;

    return decoder;
}

void
dvda_close_pcmdecoder(PCMDecoder* decoder)
{
    free(decoder);
}

void
dvda_pcmdecoder_decode_params(BitstreamReader *packet_reader,
                              struct stream_parameters* parameters)
{
    unsigned first_audio_frame;
    unsigned crc;

    packet_reader->parse(
        packet_reader,
        "16u 8p 4u 4u 4u 4u 8p 8u 8p 8u",
        &first_audio_frame,
        &(parameters->group_0_bps),
        &(parameters->group_1_bps),
        &(parameters->group_0_rate),
        &(parameters->group_1_rate),
        &(parameters->channel_assignment),
        &crc);
}

unsigned
dvda_pcmdecoder_decode_packet(PCMDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples)
{
    const static uint8_t AOB_BYTE_SWAP[2][6][36] = {
        { /*16 bps*/
            { 1,  0,  3,  2},                                 /*1 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6},                 /*2 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10}, /*3 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10,
             13, 12, 15, 14},                                 /*4 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10,
             13, 12, 15, 14, 17, 16, 19, 18},                 /*5 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10,
             13, 12, 15, 14, 17, 16, 19, 18, 21, 20, 23, 22}  /*6 ch*/
        },
        { /*24 bps*/
            {   2,  1,  5,  4,  0,  3},  /*1 ch*/
            {   2,  1,  5,  4,  8,  7,
               11, 10,  0,  3,  6,  9},  /*2 ch*/
            {   8,  7, 17, 16,  6, 15,
                2,  1,  5,  4, 11, 10,
               14, 13,  0,  3,  9, 12},  /*3 ch*/
            {   8,  7, 11, 10, 20, 19,
               23, 22,  6,  9, 18, 21,
                2,  1,  5,  4, 14, 13,
               17, 16,  0,  3, 12, 15},  /*4 ch*/
            {   8,  7, 11, 10, 14, 13,
               23, 22, 26, 25, 29, 28,
                6,  9, 12, 21, 24, 27,
                2,  1,  5,  4, 17, 16,
               20, 19,  0,  3, 15, 18},  /*5 ch*/
            {   8,  7, 11, 10, 26, 25,
               29, 28,  6,  9, 24, 27,
                2,  1,  5,  4, 14, 13,
               17, 16, 20, 19, 23, 22,
               32, 31, 35, 34,  0,  3,
               12, 15, 18, 21, 30, 33}  /*6 ch*/
        }
    };
    const unsigned bps = decoder->bps;
    int (*converter)(unsigned char *) = decoder->converter;
    const unsigned channels = decoder->channels;
    const unsigned bytes_per_sample = decoder->bytes_per_sample;
    const unsigned chunk_size = decoder->chunk_size;
    unsigned processed_frames = 0;
    br_read_f read = packet_reader->read;

    while (packet_reader->size(packet_reader) >= chunk_size) {
        uint8_t unswapped[36];
        uint8_t* unswapped_ptr = unswapped;
        unsigned i;

        /*swap read bytes to proper order*/
        for (i = 0; i < chunk_size; i++) {
            unswapped[AOB_BYTE_SWAP[bps][channels - 1][i]] =
                (uint8_t)(read(packet_reader, 8));
        }

        /*decode bytes to PCM ints and place them in proper channels*/
        for (i = 0; i < (channels * 2); i++) {
            a_int* channel = samples->_[i % channels];
            channel->append(channel, converter(unswapped_ptr));
            unswapped_ptr += bytes_per_sample;
        }

        processed_frames += 2;
    }

    return processed_frames;
}

static int
SL16_char_to_int(unsigned char *s)
{
    if (s[1] & 0x80) {
        /*negative*/
        return -(int)(0x10000 - ((s[1] << 8) | s[0]));
    } else {
        /*positive*/
        return (int)(s[1] << 8) | s[0];
    }
}

static int
SL24_char_to_int(unsigned char *s)
{
    if (s[2] & 0x80) {
        /*negative*/
        return -(int)(0x1000000 - ((s[2] << 16) | (s[1] << 8) | s[0]));
    } else {
        /*positive*/
        return (int)((s[2] << 16) | (s[1] << 8) | s[0]);
    }
}
