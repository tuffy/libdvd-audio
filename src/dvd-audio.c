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
#include "pcm.h"
#include "mlp.h"
#include "stream_parameters.h"

#define SECTOR_SIZE 2048
#define AUDIO_STREAM_ID 0xBD
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
    AOB_Reader* aob_reader;

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
void
disc_path_init(struct disc_path *path,
               const char *audio_ts_path,
               const char *device);

/*clones the values of the source disc_path to target*/
void
disc_path_copy(const struct disc_path *source,
               struct disc_path *target);

/*frees the values in the given disc_path*/
void
disc_path_free(struct disc_path *path);

/*given a full path to the AUDIO_TS.IFO file
  returns the disc's title set count
  or 0 if an error occurs opening or parsing the file*/
static unsigned
get_titleset_count(const char *audio_ts_ifo);

static DVDA_Track_Reader*
open_pcm_track_reader(AOB_Reader* aob_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size);

static DVDA_Track_Reader*
open_mlp_track_reader(AOB_Reader* aob_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size);

/*returns 0 on success, 1 on failure*/
int
read_pack_header(BitstreamReader *sector_reader,
                 uint64_t *pts,
                 unsigned *SCR_extension,
                 unsigned *bitrate);

/*returns the next packet in the sector and its stream ID
  or NULL if some error occurs reading the packet
  this does not include the 48 bit packet header*/
static BitstreamReader*
read_packet(BitstreamReader* sector_reader, unsigned* stream_id);

/*reads the header of an audio packet following the 48 bit packet header*/
void
read_audio_packet_header(BitstreamReader* packet_reader,
                         unsigned *codec_id,
                         unsigned *pad_2_size);

/*returns 0 on success, 1 on failure*/
int
decode_sector(DVDA_Track_Reader* reader, aa_int* samples);

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
    DVDA_Track_Reader* track_reader;
    uint8_t sector[SECTOR_SIZE];
    BitstreamReader* sector_reader;
    BitstreamReader* audio_packet = NULL;
    uint64_t current_PTS;
    unsigned SCR_extension;
    unsigned bitrate;
    unsigned stream_id;
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

    /*read pack header for PTS offset*/
    if (aob_reader_read(aob_reader, sector)) {
        aob_reader_close(aob_reader);
        return NULL;
    }

    sector_reader = br_open_buffer(sector, SECTOR_SIZE, BS_BIG_ENDIAN);

    if (read_pack_header(sector_reader,
                         &current_PTS,
                         &SCR_extension,
                         &bitrate)) {
        sector_reader->close(sector_reader);
        aob_reader_close(aob_reader);
        return NULL;
    }

    /*look for first audio packet*/
    do {
        audio_packet = read_packet(sector_reader, &stream_id);
        if (!audio_packet) {
            sector_reader->close(sector_reader);
            aob_reader_close(aob_reader);
            return NULL;
        } else if (stream_id == AUDIO_STREAM_ID) {
            break;
        } else {
            audio_packet->close(audio_packet);
            audio_packet = NULL;
        }
    } while (sector_reader->size(sector_reader));

    sector_reader->close(sector_reader);

    if (!audio_packet) {
        /*got to end of sector without hitting an audio packet*/
        aob_reader_close(aob_reader);
        return NULL;
    }

    read_audio_packet_header(audio_packet, &codec_id, &pad_2_size);

    switch (codec_id) {
    case PCM_CODEC_ID:
        track_reader = open_pcm_track_reader(aob_reader,
                                             audio_packet,
                                             track->pts_length,
                                             pad_2_size);
        break;
    case MLP_CODEC_ID:
        track_reader = open_mlp_track_reader(aob_reader,
                                             audio_packet,
                                             track->pts_length,
                                             pad_2_size);
        break;
    default:  /*unknown codec ID*/
        track_reader = NULL;
        aob_reader_close(aob_reader);
        break;
    }

    audio_packet->close(audio_packet);

    return track_reader;
}

void
dvda_close_track_reader(DVDA_Track_Reader* reader)
{
    aob_reader_close(reader->aob_reader);
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
dvda_channel_assignment(DVDA_Track_Reader* reader)
{
    return reader->parameters.channel_assignment;
}

uint64_t
dvda_total_pcm_frames(DVDA_Track_Reader* reader)
{
    return reader->total_pcm_frames;
}

unsigned
dvda_riff_wave_channel_mask(unsigned channel_assignment)
{
    enum { fL=0x001,
           fR=0x002,
           fC=0x004,
          LFE=0x008,
           bL=0x010,
           bR=0x020,
           bC=0x100};

    switch (channel_assignment) {
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
    unsigned i;
    unsigned c;

    if (!to_read) {
        return 0;
    }

    /*populate per-channel buffer with samples as needed*/
    while (channel_data->_[0]->len < to_read) {
        if (decode_sector(reader, channel_data)) {
            break;
        }
    }

    /*transfer contents of per-channel buffer to output buffer*/
    for (c = 0; c < channel_count; c++) {
        a_int* channel = channel_data->_[c];

        for (i = 0; i < to_read; i++) {
            buffer[i * channel_count + c] = channel->_[i];
        }

        /*remove output buffer contents from per-channel buffer*/
        channel->de_head(channel, to_read, channel);
    }

    reader->remaining_pcm_frames -= to_read;

    return to_read;
}

/*******************************************************************
 *                  private function implementations               *
 *******************************************************************/

void
disc_path_init(struct disc_path *path,
               const char *audio_ts_path,
               const char *device)
{
    path->audio_ts = strdup(audio_ts_path);
    path->device = device ? strdup(device) : NULL;
}

void
disc_path_copy(const struct disc_path *source,
               struct disc_path *target)
{
    disc_path_init(target, source->audio_ts, source->device);
}

void
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
open_pcm_track_reader(AOB_Reader* aob_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size)
{
    unsigned channel_count;
    unsigned c;
    double pts_length_d = pts_length;

    DVDA_Track_Reader* track_reader = malloc(sizeof(DVDA_Track_Reader));
    track_reader->aob_reader = aob_reader;

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
open_mlp_track_reader(AOB_Reader* aob_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size)
{
    unsigned sync_words;
    unsigned stream_type;
    unsigned channel_count;
    unsigned c;
    double pts_length_d = pts_length;
    br_pos_t* mlp_frame_start;

    DVDA_Track_Reader* track_reader = malloc(sizeof(DVDA_Track_Reader));
    track_reader->aob_reader = aob_reader;

    track_reader->codec = DVDA_MLP;

    /*FIXME - check for I/O errors?*/

    /*skip initial padding*/
    audio_packet->skip_bytes(audio_packet, pad_2_size);

    /*walk through packet looking for first MLP frame*/
    /*FIXME*/

    /*pull stream attributes from frame's major sync*/
    mlp_frame_start = audio_packet->getpos(audio_packet);
    audio_packet->parse(audio_packet, "4p 12p 16p 24u 8u",
                        &sync_words, &stream_type);

    if ((sync_words == 0xF8726F) && (stream_type == 0xBB)) {
        audio_packet->parse(audio_packet, "4u 4u 4u 4u 11p 5u 48p",
                            &(track_reader->parameters.group_0_bps),
                            &(track_reader->parameters.group_1_bps),
                            &(track_reader->parameters.group_0_rate),
                            &(track_reader->parameters.group_1_rate),
                            &(track_reader->parameters.channel_assignment));
        audio_packet->setpos(audio_packet, mlp_frame_start);
        mlp_frame_start->del(mlp_frame_start);
    } else {
        /*FIXME - continue walking the packet data looking for a sync*/
        printf("major sync not found\n");
        mlp_frame_start->del(mlp_frame_start);
        abort();
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
                                  audio_packet,
                                  track_reader->channel_data);

    return track_reader;
}

int
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

static BitstreamReader*
read_packet(BitstreamReader* sector_reader, unsigned* stream_id)
{

    if (!setjmp(*br_try(sector_reader))) {
        unsigned start_code;
        unsigned packet_length;
        BitstreamReader *packet_data;

        sector_reader->parse(sector_reader,
                             "24u 8u 16u",
                             &start_code,
                             stream_id,
                             &packet_length);

        if (start_code != 1) {
            br_etry(sector_reader);
            return NULL;
        }

        packet_data = sector_reader->substream(sector_reader,
                                               packet_length);

        br_etry(sector_reader);
        return packet_data;
    } else {
        br_etry(sector_reader);
        return NULL;
    }
}

void
read_audio_packet_header(BitstreamReader* packet_reader,
                         unsigned *codec_id,
                         unsigned *pad_2_size)
{
    unsigned pad_1_size;

    packet_reader->parse(packet_reader, "16p 8u", &pad_1_size);
    packet_reader->skip_bytes(packet_reader, pad_1_size);
    packet_reader->parse(packet_reader, "8u 8p 8p 8u", codec_id, pad_2_size);
}

int
decode_sector(DVDA_Track_Reader* reader, aa_int* samples)
{
    uint8_t sector[SECTOR_SIZE];
    BitstreamReader* sector_reader;
    uint64_t pts;
    unsigned SCR_extension;
    unsigned bitrate;

    /*read next sector from stream*/
    if (aob_reader_read(reader->aob_reader, sector)) {
        return 1;
    }

    sector_reader = br_open_buffer(sector, SECTOR_SIZE, BS_BIG_ENDIAN);

    /*read pack header*/
    if (read_pack_header(sector_reader, &pts, &SCR_extension, &bitrate)) {
        sector_reader->close(sector_reader);
        return 1;
    }

    /*for each audio packet in sector*/
    do {
        /*ensure codec matches our decoder's codec*/
        unsigned stream_id;
        BitstreamReader* packet_reader = read_packet(sector_reader, &stream_id);

        if (!packet_reader) {
            sector_reader->close(sector_reader);
            return 1;
        } else if (stream_id == AUDIO_STREAM_ID) {
            unsigned codec_id;
            unsigned pad_2_size;

            if (!setjmp(*br_try(packet_reader))) {

                read_audio_packet_header(packet_reader,
                                         &codec_id,
                                         &pad_2_size);

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

                        dvda_pcmdecoder_decode_packet(reader->decoder.pcm,
                                                      packet_reader,
                                                      samples);
                    }
                    break;
                case MLP_CODEC_ID:
                    packet_reader->skip_bytes(packet_reader, pad_2_size);
                    dvda_mlpdecoder_decode_packet(reader->decoder.mlp,
                                                  packet_reader,
                                                  samples);
                    break;
                default:
                    /*unknown audio codec, so ignore packet*/
                    break;
                }

                br_etry(packet_reader);
            } else {
                /*some I/O error reading from packet*/
                br_etry(packet_reader);
                packet_reader->close(packet_reader);
                sector_reader->close(sector_reader);
                return 1;
            }
        }

        packet_reader->close(packet_reader);
    } while (sector_reader->size(sector_reader));

    sector_reader->close(sector_reader);

    return 0;
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
