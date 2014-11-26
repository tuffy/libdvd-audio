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

#include "packet.h"

#define AUDIO_STREAM_ID 0xBD
#define SECTOR_SIZE 2048

/*returns 0 on success, 1 on failure*/
static int
read_pack_header(BitstreamReader *sector_reader,
                 uint64_t *pts,
                 unsigned *SCR_extension,
                 unsigned *bitrate);

struct Packet_Reader_s {
    AOB_Reader *aob_reader;
    BitstreamQueue *sector_data;
};

Packet_Reader*
packet_reader_open(AOB_Reader *aob_reader)
{
    Packet_Reader *packet_reader = malloc(sizeof(Packet_Reader));
    packet_reader->aob_reader = aob_reader;
    packet_reader->sector_data = br_open_queue(BS_BIG_ENDIAN);
    return packet_reader;
}

void
packet_reader_free(Packet_Reader *packet_reader)
{
    packet_reader->sector_data->close(packet_reader->sector_data);
    free(packet_reader);
}

void
packet_reader_close(Packet_Reader *packet_reader)
{
    aob_reader_close(packet_reader->aob_reader);
    packet_reader_free(packet_reader);
}

BitstreamReader*
packet_reader_next_packet(Packet_Reader *packet_reader,
                          unsigned *stream_id,
                          unsigned *sector)
{
    BitstreamQueue *sector_data = packet_reader->sector_data;
    BitstreamReader *sector_reader = (BitstreamReader*)sector_data;

    if (sector_data->size(sector_data) == 0) {
        uint8_t sector_buffer[SECTOR_SIZE];
        uint64_t pts;
        unsigned SCR_extension;
        unsigned bitrate;

        /*ran out of sector data, so read another sector (if possible)*/
        if (aob_reader_read(packet_reader->aob_reader, sector_buffer)) {
            /*some error reading the next .AOB packet*/
            return NULL;
        }

        /*read pack header from sector data*/
        sector_data->push(sector_data, SECTOR_SIZE, sector_buffer);
        if (read_pack_header(sector_reader, &pts, &SCR_extension, &bitrate)) {
            return NULL;
        }
    }

    /*current sector always 1 ahead of the one being read from*/
    *sector = aob_reader_tell(packet_reader->aob_reader) - 1;

    /*read next packet from sector reader*/
    if (!setjmp(*br_try(sector_reader))) {
        unsigned start_code;
        unsigned packet_data_length;
        BitstreamReader *packet_data;

        /*read 48 bit packet header*/
        sector_reader->parse(sector_reader, "24u 8u 16u",
                             &start_code, stream_id, &packet_data_length);

        /*ensure start code is correct*/
        if (start_code != 0x000001) {
            br_etry(sector_reader);
            return NULL;
        }

        /*read packet data itself*/
        packet_data = sector_reader->substream(sector_reader,
                                               packet_data_length);

        br_etry(sector_reader);
        return packet_data;
    } else {
        br_etry(sector_reader);
        return NULL;
    }
}

BitstreamReader*
packet_reader_next_audio_packet(Packet_Reader *packet_reader,
                                unsigned *sector)
{
    unsigned stream_id = 0;
    BitstreamReader *packet = packet_reader_next_packet(packet_reader,
                                                        &stream_id,
                                                        sector);
    if (!packet) {
        return NULL;
    }
    if (stream_id == AUDIO_STREAM_ID) {
        return packet;
    } else {
        packet->close(packet);
        return packet_reader_next_audio_packet(packet_reader, sector);
    }
}

static int
read_pack_header(BitstreamReader *sector_reader,
                 uint64_t *pts,
                 unsigned *SCR_extension,
                 unsigned *bitrate)
{
    if (!setjmp(*br_try(sector_reader))) {
        unsigned sync_bytes;
        unsigned pad[6];
        unsigned PTS_high;
        unsigned PTS_mid;
        unsigned PTS_low;
        unsigned stuffing_count;

        sector_reader->parse(
            sector_reader,
            "32u 2u 3u 1u 15u 1u 15u 1u 9u 1u 22u 2u 5p 3u",
            &sync_bytes,     /*32 bits*/
            &(pad[0]),       /* 2 bits*/
            &PTS_high,       /* 3 bits*/
            &(pad[1]),       /* 1 bit */
            &PTS_mid,        /*15 bits*/
            &(pad[2]),       /* 1 bit */
            &PTS_low,        /*15 bits*/
            &(pad[3]),       /* 1 bit */
            SCR_extension,   /* 9 bits*/
            &(pad[4]),       /* 1 bit */
            bitrate,         /*22 bits*/
            &(pad[5]),       /* 2 bits*/
            &stuffing_count  /* 3 bits*/
            );

        sector_reader->skip(sector_reader, 8 * stuffing_count);

        br_etry(sector_reader);

        if (sync_bytes != 0x000001BA) {
            return 1;
        }

        if ((pad[0] == 1) && (pad[1] == 1) && (pad[2] == 1) &&
            (pad[3] == 1) && (pad[4] == 1) && (pad[5] == 3)) {
            *pts = (PTS_high << 30) | (PTS_mid << 15) | PTS_low;
            return 0;
        } else {
            return 1;
        }
    } else {
        br_etry(sector_reader);
        return 1;
    }
}
