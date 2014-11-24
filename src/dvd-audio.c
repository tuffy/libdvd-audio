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

#include "dvd-audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bitstream.h"
#include "array.h"
#include "audio_ts.h"
#include "aob.h"
#include "packet.h"
#include "pcm.h"
#include "mlp.h"
#include "stream_parameters.h"

#define SECTOR_SIZE 2048
#define PCM_CODEC_ID 0xA0
#define MLP_CODEC_ID 0xA1

/*******************************************************************
 *                       structure definitions                     *
 *******************************************************************/

struct disc_path {
    char *audio_ts;
    char *device;
};

struct DVDA_s {
    struct disc_path disc;

    unsigned titleset_count;
};

struct DVDA_Titleset_s {
    struct disc_path disc;

    unsigned titleset_number;

    char *ats_xx_ifo_path;
    unsigned title_count;
};

struct DVDA_Index_s {
    unsigned first_sector;
    unsigned last_sector;
};

struct DVDA_Title_s {
    struct disc_path disc;

    unsigned titleset_number;
    unsigned title_number;

    unsigned track_count;
    unsigned index_count;
    unsigned pts_length;
    struct {
        unsigned index_number;
        unsigned pts_index;
        unsigned pts_length;
    } tracks[256];
    DVDA_Index indexes[256];
};

struct DVDA_Track_s {
    struct disc_path disc;

    unsigned titleset_number;
    unsigned title_number;
    unsigned track_number;

    unsigned pts_index;
    unsigned pts_length;

    DVDA_Index index;
};

struct DVDA_Track_Reader_s {
    Packet_Reader* packet_reader;

    dvda_codec_t codec;
    struct stream_parameters parameters;
    uint64_t total_pcm_frames;
    uint64_t remaining_pcm_frames;

    union {
        PCMDecoder* pcm;
        MLPDecoder* mlp;
    } decoder;

    aa_int* channel_data;
};

/*******************************************************************
 *                   private function definitions                  *
 *******************************************************************/

/*initializes a disc_path structure with the given paths*/
static void
disc_path_init(struct disc_path *path,
               const char *audio_ts_path,
               const char *device);

/*clones the values of the source disc_path to target*/
static void
disc_path_copy(const struct disc_path *source,
               struct disc_path *target);

/*frees the values in the given disc_path*/
static void
disc_path_free(struct disc_path *path);

/*given a full path to the AUDIO_TS.IFO file
  returns the disc's title set count
  or 0 if an error occurs opening or parsing the file*/
static unsigned
get_titleset_count(const char *audio_ts_ifo);

static DVDA_Track_Reader*
open_pcm_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size);

static DVDA_Track_Reader*
open_mlp_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size);

/*reads the header of an audio packet following the 48 bit packet header*/
static void
read_audio_packet_header(BitstreamReader* packet_reader,
                         unsigned *codec_id,
                         unsigned *pad_2_size);

/*returns the aount of PCM frames read*/
static unsigned
decode_audio_packet(DVDA_Track_Reader* reader,
                    BitstreamReader* packet_reader,
                    aa_int* samples);

/*given an AOB reader and packet data (not including header or pad 2 block)
  locates the first set of stream parameters
  and dumps the remaining MLP data in mlp_data
  for later processing

  returns the number of bytes skipped to reach the parameters*/
static unsigned
locate_mlp_parameters(Packet_Reader* packet_reader,
                      BitstreamReader* packet_data,
                      struct stream_parameters* parameters,
                      BitstreamQueue* mlp_data);

/*given a 4 bit packed field,
  returns the bits-per-sample which is either 16, 20 or 24*/
static unsigned
unpack_bits_per_sample(unsigned packed_field);

/*given a 4 bit packed field,
  returns the sample rate in Hz which is either
  44100, 48000, 88200, 96000 176400 or 192000*/
static unsigned
unpack_sample_rate(unsigned packed_field);

/*given a 5 bit packed field,
  returns the channel count which is between 1 and 6*/
static unsigned
unpack_channel_count(unsigned packet_field);

/*******************************************************************
 *                  public function implementations                *
 *******************************************************************/

DVDA*
dvda_open(const char *audio_ts_path, const char *device)
{
    DVDA *dvda;
    char *audio_ts_ifo;
    unsigned titleset_count;

    if (!audio_ts_path)
        return NULL;

    if ((audio_ts_ifo = find_audio_ts_file(audio_ts_path,
                                           "audio_ts.ifo")) == NULL) {
        /*unable to find AUDIO_TS.IFO*/
        return NULL;
    }

    titleset_count = get_titleset_count(audio_ts_ifo);

    free(audio_ts_ifo);

    if (titleset_count == 0) {
        /*unable to open AUDIO_TS.IFO or some parse error*/
        return NULL;
    }

    dvda = malloc(sizeof(DVDA));
    disc_path_init(&dvda->disc, audio_ts_path, device);
    dvda->titleset_count = titleset_count;
    return dvda;
}

void
dvda_close(DVDA *dvda)
{
    disc_path_free(&dvda->disc);
    free(dvda);
}

unsigned
dvda_titleset_count(const DVDA *dvda)
{
    return dvda->titleset_count;
}

DVDA_Titleset*
dvda_open_titleset(DVDA* dvda, unsigned titleset_num)
{
    char ats_xx_ifo_name[13];
    char *ats_xx_ifo_path;
    FILE *ats_xx_ifo;
    BitstreamReader *bs;
    DVDA_Titleset *titleset;

    snprintf(ats_xx_ifo_name, 13, "ATS_%2.2d_0.IFO", MIN(titleset_num, 99));

    if ((ats_xx_ifo_path = find_audio_ts_file(dvda->disc.audio_ts,
                                              ats_xx_ifo_name)) == NULL) {
        /*unable to find requested .IFO file*/
        return NULL;
    }

    if ((ats_xx_ifo = fopen(ats_xx_ifo_path, "rb")) == NULL) {
        free(ats_xx_ifo_path);
        return NULL;
    }

    titleset = malloc(sizeof(DVDA_Titleset));

    disc_path_copy(&dvda->disc, &titleset->disc);

    titleset->titleset_number = titleset_num;

    titleset->ats_xx_ifo_path = ats_xx_ifo_path;
    titleset->title_count = 0;  /*placeholder*/

    bs = br_open(ats_xx_ifo, BS_BIG_ENDIAN);
    if (!setjmp(*br_try(bs))) {
        uint8_t identifier[12];
        const uint8_t dvdaudio_ats[12] =
            {68, 86, 68, 65, 85, 68, 73, 79, 45, 65, 84, 83};
        unsigned last_byte_address;

        bs->read_bytes(bs, identifier, 12);

        if (memcmp(identifier, dvdaudio_ats, 12)) {
            /*some identifier mismatch*/
            br_abort(bs);
        }

        bs->seek(bs, SECTOR_SIZE, BS_SEEK_SET);

        bs->parse(bs, "16u 16p 32u",
                  &titleset->title_count, &last_byte_address);

        br_etry(bs);
        bs->close(bs);

        return titleset;
    } else {
        br_etry(bs);
        bs->close(bs);
        dvda_close_titleset(titleset);
        return NULL;
    }
}

void
dvda_close_titleset(DVDA_Titleset* titleset)
{
    disc_path_free(&titleset->disc);

    free(titleset->ats_xx_ifo_path);

    free(titleset);
}

unsigned
dvda_titleset_number(const DVDA_Titleset* titleset)
{
    return titleset->titleset_number;
}

unsigned
dvda_title_count(const DVDA_Titleset* titleset)
{
    return titleset->title_count;
}

DVDA_Title*
dvda_open_title(DVDA_Titleset* titleset, unsigned title_num)
{
    FILE *ats_xx_ifo = fopen(titleset->ats_xx_ifo_path, "rb");
    BitstreamReader *bs;
    unsigned title_count;
    unsigned i;
    DVDA_Title* title;

    if (!ats_xx_ifo) {
        return NULL;
    }

    bs = br_open(ats_xx_ifo, BS_BIG_ENDIAN);
    title = malloc(sizeof(DVDA_Title));

    disc_path_copy(&titleset->disc, &title->disc);

    title->titleset_number = titleset->titleset_number;
    title->title_number = title_num;

    title->track_count = 0; /*placeholder*/
    title->index_count = 0; /*placeholder*/
    title->pts_length = 0; /*placeholder*/

    if (!setjmp(*br_try(bs))) {
        bs->seek(bs, SECTOR_SIZE, BS_SEEK_SET);
        bs->parse(bs, "16u 16p 32p", &title_count);
        assert(title_count == titleset->title_count);
        for (i = 0; i < title_count; i++) {
            unsigned title_number;
            unsigned title_table_offset;
            bs->parse(bs, "8u 24p 32u", &title_number, &title_table_offset);
            /*ignore title number in stream and use the index*/
            if (title_num == (i + 1)) {
                unsigned sector_pointers_offset;
                unsigned i;

                bs->seek(bs, SECTOR_SIZE + title_table_offset, BS_SEEK_SET);
                bs->parse(bs, "16p 8u 8u 32u 32p 16u 16p",
                          &title->track_count,
                          &title->index_count,
                          &title->pts_length,
                          &sector_pointers_offset);

                /*populate tracks*/
                for (i = 0; i < title->track_count; i++) {
                    bs->parse(bs, "32p 8u 8p 32u 32u 48p",
                              &(title->tracks[i].index_number),
                              &(title->tracks[i].pts_index),
                              &(title->tracks[i].pts_length));
                }

                /*populate indexes*/
                bs->seek(bs,
                         SECTOR_SIZE +
                         title_table_offset +
                         sector_pointers_offset,
                         BS_SEEK_SET);
                for (i = 0; i < title->index_count; i++) {
                    unsigned index_id;
                    bs->parse(bs, "32u 32u 32u",
                              &index_id,
                              &(title->indexes[i].first_sector),
                              &(title->indexes[i].last_sector));
                    if (index_id != 0x1000000) {
                        /*invalid index ID*/
                        br_abort(bs);
                    }
                }

                br_etry(bs);
                bs->close(bs);

                return title;
            }
        }

        /*title not found*/
        br_etry(bs);
        bs->close(bs);
        dvda_close_title(title);
        return NULL;
    } else {
        /*some I/O or parse error reading ATS_XX_0.IFO*/
        br_etry(bs);
        bs->close(bs);
        dvda_close_title(title);
        return NULL;
    }
}

void
dvda_close_title(DVDA_Title* title)
{
    disc_path_free(&title->disc);

    free(title);
}

unsigned
dvda_title_number(const DVDA_Title* title)
{
    return title->title_number;
}

unsigned
dvda_track_count(const DVDA_Title* title)
{
    return title->track_count;
}

unsigned
dvda_title_pts_length(const DVDA_Title* title)
{
    return title->pts_length;
}

DVDA_Track*
dvda_open_track(DVDA_Title* title, unsigned track_num)
{
    DVDA_Track* track;

    if ((track_num == 0) || (track_num > title->track_count)) {
        return NULL;
    }

    track = malloc(sizeof(DVDA_Track));

    disc_path_copy(&title->disc, &track->disc);

    track->titleset_number = title->titleset_number;
    track->title_number = title->title_number;
    track->track_number = track_num;

    track->pts_index =
        title->tracks[track_num - 1].pts_index;
    track->pts_length =
        title->tracks[track_num - 1].pts_length;
    track->index =
        title->indexes[title->tracks[track_num - 1].index_number - 1];

    return track;
}

void
dvda_close_track(DVDA_Track* track)
{
    disc_path_free(&track->disc);

    free(track);
}

unsigned
dvda_track_number(const DVDA_Track* track)
{
    return track->track_number;
}

unsigned
dvda_track_pts_index(const DVDA_Track* track)
{
    return track->pts_index;
}

unsigned
dvda_track_pts_length(const DVDA_Track* track)
{
    return track->pts_length;
}

unsigned
dvda_track_first_sector(const DVDA_Track* track)
{
    return track->index.first_sector;
}

unsigned
dvda_track_last_sector(const DVDA_Track* track)
{
    return track->index.last_sector;
}

DVDA_Track_Reader*
dvda_open_track_reader(const DVDA_Track* track)
{
    AOB_Reader* aob_reader;
    Packet_Reader* packet_reader;
    BitstreamReader* audio_packet = NULL;
    DVDA_Track_Reader* track_reader;
    unsigned codec_id;
    unsigned pad_2_size;

    /*open an AOB reader for the given disc*/
    if ((aob_reader = aob_reader_open(track->disc.audio_ts,
                                      track->disc.device,
                                      track->titleset_number)) == NULL) {
        return NULL;
    }

    /*seek to the track's first sector*/
    if (aob_reader_seek(aob_reader, track->index.first_sector)) {
        aob_reader_close(aob_reader);
        return NULL;
    }

    /*wrap AOB reader with packet reader*/
    packet_reader = packet_reader_open(aob_reader);

    /*get first audio packet from packet reader*/
    audio_packet = packet_reader_next_audio_packet(packet_reader);

    if (!audio_packet) {
        /*got to end of stream without hitting an audio packet*/
        packet_reader_close(packet_reader);
        return NULL;
    }

    read_audio_packet_header(audio_packet, &codec_id, &pad_2_size);

    switch (codec_id) {
    case PCM_CODEC_ID:
        track_reader = open_pcm_track_reader(packet_reader,
                                             audio_packet,
                                             track->pts_length,
                                             pad_2_size);
        break;
    case MLP_CODEC_ID:
        track_reader = open_mlp_track_reader(packet_reader,
                                             audio_packet,
                                             track->pts_length,
                                             pad_2_size);
        break;
    default:  /*unknown codec ID*/
        track_reader = NULL;
        packet_reader_close(packet_reader);
        break;
    }

    audio_packet->close(audio_packet);

    return track_reader;
}

void
dvda_close_track_reader(DVDA_Track_Reader* reader)
{
    packet_reader_close(reader->packet_reader);
    switch (reader->codec) {
    case DVDA_PCM:
        dvda_close_pcmdecoder(reader->decoder.pcm);
        break;
    case DVDA_MLP:
        dvda_close_mlpdecoder(reader->decoder.mlp);
        break;
    }
    reader->channel_data->del(reader->channel_data);
    free(reader);
}

dvda_codec_t
dvda_codec(DVDA_Track_Reader* reader)
{
    return reader->codec;
}

unsigned
dvda_bits_per_sample(DVDA_Track_Reader* reader)
{
    return unpack_bits_per_sample(reader->parameters.group_0_bps);
}

unsigned
dvda_sample_rate(DVDA_Track_Reader* reader)
{
    return unpack_sample_rate(reader->parameters.group_0_rate);
}

unsigned
dvda_channel_count(DVDA_Track_Reader* reader)
{
    return unpack_channel_count(reader->parameters.channel_assignment);
}

unsigned
dvda_riff_wave_channel_mask(DVDA_Track_Reader *reader)
{
    enum { fL=0x001,
           fR=0x002,
           fC=0x004,
          LFE=0x008,
           bL=0x010,
           bR=0x020,
           bC=0x100};

    switch (reader->parameters.channel_assignment) {
    case 0:  /*front center*/
        return fC;
    case 1:  /*front left, front right*/
        return fL | fR;
    case 2:  /*front left, front right, back center*/
        return fL | fR | bC;
    case 3:  /*front left, front right, back left, back right*/
        return fL | fR | bL | bR;
    case 4:  /*front left, front right, LFE*/
        return fL | fR | LFE;
    case 5:  /*front left, front right, LFE, back center*/
        return fL | fR | LFE | bC;
    case 6:  /*front left, front right, LFE, back left, back right*/
        return fL | fR | LFE | bL | bR;
    case 7:  /*front left, front right, front center*/
        return fL | fR | fC;
    case 8:  /*front left, front right, front center, back center*/
        return fL | fR | fC | bC;
    case 9:  /*front left, front right, front center, back left, back right*/
        return fL | fR | fC | bL | bR;
    case 10: /*front left, front right, front center, LFE*/
        return fL | fR | fC | LFE;
    case 11: /*front left, front right, front center, LFE, back center*/
        return fL | fR | fC | LFE | bC;
    case 12: /*front left, front right, front center,
               LFE, back left, back right*/
        return fL | fR | fC | LFE | bL | bR;
    case 13: /*front left, front right, front center, back center*/
        return fL | fR | fC | bC;
    case 14: /*front left, front right, front center, back left, back right*/
        return fL | fR | fC | bL | bR;
    case 15: /*front left, front right, front center, LFE*/
        return fL | fR | fC | LFE;
    case 16: /*front left, front right, front center, LFE, back center*/
        return fL | fR | fC | LFE | bC;
    case 17: /*front left, front right, front center,
               LFE, back left, back right*/
        return fL | fR | fC | LFE | bL | bR;
    case 18: /*front left, front right, back left, back right, LFE*/
        return fL | fR | bL | bR | LFE;
    case 19: /*front left, front right, back left, back right, front center*/
        return fL | fR | bL | bR | fC;
    case 20: /*front left, front right, back left, back right,
               front center, LFE*/
        return fL | fR | bL | bR | fC | LFE;
    default:
        return 0;
    }
}

unsigned
dvda_read(DVDA_Track_Reader* reader,
          unsigned pcm_frames,
          int buffer[])
{
    const unsigned to_read = MIN(reader->remaining_pcm_frames, pcm_frames);
    const unsigned channel_count = dvda_channel_count(reader);
    aa_int* channel_data = reader->channel_data;
    unsigned amount_read;
    unsigned c;

    if (!to_read) {
        return 0;
    }

    /*populate per-channel buffer with samples as needed*/
    while (channel_data->_[0]->len < to_read) {
        BitstreamReader *audio_packet =
            packet_reader_next_audio_packet(reader->packet_reader);
        if (audio_packet) {
            const unsigned pcm_frames_read =
                decode_audio_packet(reader, audio_packet, channel_data);

            audio_packet->close(audio_packet);

            if (!pcm_frames_read) {
                /*no more data from next audio packet*/
                break;
            }
        } else {
            /*no more audio packets in stream*/
            break;
        }
    }

    amount_read = MIN(to_read, channel_data->_[0]->len);

    /*transfer contents of per-channel buffer to output buffer*/
    for (c = 0; c < channel_count; c++) {
        a_int* channel = channel_data->_[c];
        unsigned i;
        assert(channel->len >= amount_read);

        for (i = 0; i < amount_read; i++) {
            buffer[i * channel_count + c] = channel->_[i];
        }

        /*remove output buffer contents from per-channel buffer*/
        channel->de_head(channel, amount_read, channel);
    }

    if (amount_read == to_read) {
        reader->remaining_pcm_frames -= amount_read;
    } else {
        reader->remaining_pcm_frames = 0;
    }

    return amount_read;
}

/*******************************************************************
 *                  private function implementations               *
 *******************************************************************/

static void
disc_path_init(struct disc_path *path,
               const char *audio_ts_path,
               const char *device)
{
    path->audio_ts = strdup(audio_ts_path);
    path->device = device ? strdup(device) : NULL;
}

static void
disc_path_copy(const struct disc_path *source,
               struct disc_path *target)
{
    disc_path_init(target, source->audio_ts, source->device);
}

static void
disc_path_free(struct disc_path *path)
{
    free(path->audio_ts);
    free(path->device);
}

static unsigned
get_titleset_count(const char *audio_ts_ifo)
{
    FILE *audio_ts;
    BitstreamReader *bs;

    if ((audio_ts = fopen(audio_ts_ifo, "rb")) == NULL)
        return 0;

    bs = br_open(audio_ts, BS_BIG_ENDIAN);
    if (!setjmp(*br_try(bs))) {
        uint8_t identifier[12];
        const uint8_t dvdaudio_amg[12] =
            {68, 86, 68, 65, 85, 68, 73, 79, 45, 65, 77, 71};
        unsigned titleset_count;

        bs->parse(bs,
            "12b 32p 12P 32p 16p 4P 16p 16p 8p 4P 8p 32p 10P 8p 8u 40P",
             identifier, &titleset_count);

        br_etry(bs);
        bs->close(bs);

        if (!memcmp(identifier, dvdaudio_amg, 12)) {
            return titleset_count;
        } else {
            return 0;
        }
    } else {
        /*some error when reading file*/
        br_etry(bs);
        bs->close(bs);
        return 0;
    }
}

static DVDA_Track_Reader*
open_pcm_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size)
{
    unsigned channel_count;
    unsigned c;
    double pts_length_d = pts_length;

    DVDA_Track_Reader* track_reader = malloc(sizeof(DVDA_Track_Reader));
    track_reader->packet_reader = packet_reader;

    /*FIXME - check for I/O errors?*/

    /*pull stream attributes from start of packet*/
    /*it appears PCM data always starts at the beginning of the
      track's first sector and PCM data doesn't cross packet boundaries*/
    track_reader->codec = DVDA_PCM;
    dvda_pcmdecoder_decode_params(audio_packet, &(track_reader->parameters));

    pts_length_d *= unpack_sample_rate(track_reader->parameters.group_0_rate);
    pts_length_d /= PTS_PER_SECOND;

    track_reader->total_pcm_frames = lround(pts_length_d);
    track_reader->remaining_pcm_frames = track_reader->total_pcm_frames;

    channel_count =
        unpack_channel_count(track_reader->parameters.channel_assignment);

    track_reader->decoder.pcm = dvda_open_pcmdecoder(
        unpack_bits_per_sample(track_reader->parameters.group_0_bps),
        channel_count);

    /*setup initial channels*/
    track_reader->channel_data = aa_int_new();
    for (c = 0; c < channel_count; c++) {
        (void)track_reader->channel_data->append(track_reader->channel_data);
    }

    /*decode remaining bytes in packet to buffer*/
    audio_packet->skip_bytes(audio_packet, pad_2_size - 9);
    dvda_pcmdecoder_decode_packet(track_reader->decoder.pcm,
                                  audio_packet,
                                  track_reader->channel_data);

    return track_reader;
}

static DVDA_Track_Reader*
open_mlp_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size)
{
    unsigned channel_count;
    unsigned c;
    double pts_length_d = pts_length;
    unsigned mlp_frame_offset;
    BitstreamQueue* mlp_data;

    DVDA_Track_Reader* track_reader = malloc(sizeof(DVDA_Track_Reader));
    track_reader->packet_reader = packet_reader;

    track_reader->codec = DVDA_MLP;

    /*FIXME - check for I/O errors?*/

    /*skip initial padding*/
    audio_packet->skip_bytes(audio_packet, pad_2_size);

    mlp_data = br_open_queue(BS_BIG_ENDIAN);

    mlp_frame_offset = locate_mlp_parameters(packet_reader,
                                             audio_packet,
                                             &track_reader->parameters,
                                             mlp_data);

    if (mlp_frame_offset) {
        fprintf(stderr, "MLP frame offset : %u\n", mlp_frame_offset);
    }

    pts_length_d *= unpack_sample_rate(track_reader->parameters.group_0_rate);
    pts_length_d /= PTS_PER_SECOND;

    track_reader->total_pcm_frames = lround(pts_length_d);
    track_reader->remaining_pcm_frames = track_reader->total_pcm_frames;

    channel_count =
        unpack_channel_count(track_reader->parameters.channel_assignment);

    track_reader->decoder.mlp = dvda_open_mlpdecoder(
        &(track_reader->parameters));

    /*setup initial channels*/
    track_reader->channel_data = aa_int_new();
    for (c = 0; c < channel_count; c++) {
        (void)track_reader->channel_data->append(track_reader->channel_data);
    }

    /*decode remaining MLP frames in packet to buffer*/
    dvda_mlpdecoder_decode_packet(track_reader->decoder.mlp,
                                  (BitstreamReader*)mlp_data,
                                  track_reader->channel_data);

    mlp_data->close(mlp_data);

    return track_reader;
}

static void
read_audio_packet_header(BitstreamReader* packet_reader,
                         unsigned *codec_id,
                         unsigned *pad_2_size)
{
    unsigned pad_1_size;

    packet_reader->parse(packet_reader, "16p 8u", &pad_1_size);
    packet_reader->skip_bytes(packet_reader, pad_1_size);
    packet_reader->parse(packet_reader, "8u 8p 8p 8u", codec_id, pad_2_size);
}

static unsigned
decode_audio_packet(DVDA_Track_Reader* reader,
                    BitstreamReader* packet_reader,
                    aa_int* samples)
{
    if (!setjmp(*br_try(packet_reader))) {
        unsigned pcm_frames_read = 0;
        unsigned codec_id;
        unsigned pad_2_size;

        read_audio_packet_header(packet_reader,
                                 &codec_id,
                                 &pad_2_size);

        /*FIXME - ensure audio codec is the same as the reader's codec*/

        switch (codec_id) {
        case PCM_CODEC_ID:
            {
                struct stream_parameters parameters;

                dvda_pcmdecoder_decode_params(packet_reader,
                                              &parameters);

                if (!dvda_params_equal(&reader->parameters,
                                       &parameters)) {
                    /*some stream parameters mismatch*/
                    br_abort(packet_reader);
                }

                packet_reader->skip_bytes(packet_reader,
                                          pad_2_size - 9);

                pcm_frames_read =
                    dvda_pcmdecoder_decode_packet(reader->decoder.pcm,
                                                  packet_reader,
                                                  samples);
            }
            break;
        case MLP_CODEC_ID:
            packet_reader->skip_bytes(packet_reader, pad_2_size);
            pcm_frames_read =
                dvda_mlpdecoder_decode_packet(reader->decoder.mlp,
                                              packet_reader,
                                              samples);
            break;
        default:
            /*unknown audio codec, so ignore packet*/
            break;
        }

        br_etry(packet_reader);
        return pcm_frames_read;
    } else {
        /*some I/O error reading from packet*/
        br_etry(packet_reader);
        return 0;
    }
}

static unsigned
locate_mlp_parameters(Packet_Reader* packet_reader,
                      BitstreamReader* packet_data,
                      struct stream_parameters* parameters,
                      BitstreamQueue* mlp_data)
{
    unsigned bytes_skipped = 0;
    BitstreamReader* mlp_reader = (BitstreamReader*)mlp_data;

    packet_data->enqueue(packet_data,
                         packet_data->size(packet_data),
                         mlp_data);

    for (;;) {
        unsigned sync_words;
        unsigned stream_type;
        br_pos_t* mlp_frame_start;

        if (mlp_data->size(mlp_data) < 8) {
            /*extend queue with additional packets from packet_reader*/
            /*FIXME*/
            fprintf(stderr, "*** FIXME : data queue exhausted\n");
            abort();
        }

        /*look for major sync*/
        mlp_frame_start = mlp_reader->getpos(mlp_reader);
        mlp_reader->parse(mlp_reader, "4p 12p 16p 24u 8u",
                          &sync_words, &stream_type);

        /*if major sync is found*/
        if ((sync_words == 0xF8726F) && (stream_type == 0xBB)) {
            /*populate stream parameters from frame's major sync*/
            mlp_reader->parse(mlp_reader, "4u 4u 4u 4u 11p 5u 48p",
                              &parameters->group_0_bps,
                              &parameters->group_1_bps,
                              &parameters->group_0_rate,
                              &parameters->group_1_rate,
                              &parameters->channel_assignment);

            /*rewind to start and frame and exit*/
            mlp_reader->setpos(mlp_reader, mlp_frame_start);
            mlp_frame_start->del(mlp_frame_start);

            return bytes_skipped;
        } else {  /*if major sync is not found*/
            /*rewind to start of frame*/
            mlp_reader->setpos(mlp_reader, mlp_frame_start);

            /*advance 1 byte*/
            mlp_reader->skip(mlp_reader, 8);

            /*and continue looking*/
            bytes_skipped += 1;
        }
    }
}

static unsigned
unpack_bits_per_sample(unsigned packed_field)
{
    switch (packed_field) {
    case 0:
        return 16;
    case 1:
        return 20;
    case 2:
        return 24;
    default:
        return 0;
    }
}

static unsigned
unpack_sample_rate(unsigned packed_field)
{
    switch (packed_field) {
    case 0:
        return 48000;
    case 1:
        return 96000;
    case 2:
        return 192000;
    case 8:
        return 44100;
    case 9:
        return 88200;
    case 10:
        return 176400;
    default:
        return 0;
    }
}

static unsigned
unpack_channel_count(unsigned packed_field)
{
    switch (packed_field) {
    case 0:  /*front center*/
        return 1;
    case 1:  /*front left, front right*/
        return 2;
    case 2:  /*front left, front right, back center*/
    case 4:  /*front left, front right, LFE*/
    case 7:  /*front left, front right, front center*/
        return 3;
    case 3:  /*front left, front right, back left, back right*/
    case 5:  /*front left, front right, LFE, back center*/
    case 8:  /*front left, front right, front center, back center*/
    case 10: /*front left, front right, front center, LFE*/
    case 13: /*front left, front right, front center, back center*/
    case 15: /*front left, front right, front center, LFE*/
        return 4;
    case 6:  /*front left, front right, LFE, back left, back right*/
    case 9:  /*front left, front right, front center, back left, back right*/
    case 11: /*front left, front right, front center, LFE, back center*/
    case 14: /*front left, front right, front center, back left, back right*/
    case 16: /*front left, front right, front center, LFE, back center*/
    case 18: /*front left, front right, back left, back right, LFE*/
    case 19: /*front left, front right, back left, back right, front center*/
        return 5;
    case 12: /*front left, front right, front center,
               LFE, back left, back right*/
    case 17: /*front left, front right, front center,
               LFE, back left, back right*/
    case 20: /*front left, front right, back left, back right,
               front center, LFE*/
        return 6;
    default:
        return 0;
    }
}
