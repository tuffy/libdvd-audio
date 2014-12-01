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

struct ats_XX_0_ifo_track {
    unsigned index_number;
    struct {
        unsigned index;
        unsigned length;
    } PTS;
};

struct ats_XX_0_ifo_index {
    unsigned first_sector;
    unsigned last_sector;
};

struct ats_XX_0_ifo_title {
    unsigned track_count;
    unsigned index_count;
    unsigned PTS_length;
    struct ats_XX_0_ifo_track track[256];
    struct ats_XX_0_ifo_index index[256];
};

struct ats_XX_0_ifo {
    unsigned title_count;
    struct ats_XX_0_ifo_title *title;
};

struct DVDA_s {
    struct disc_path disc;

    unsigned titleset_count;
};

struct DVDA_Titleset_s {
    struct disc_path disc;

    unsigned titleset_number;

    struct ats_XX_0_ifo ifo;
};

struct DVDA_Title_s {
    struct disc_path disc;

    unsigned titleset_number;
    unsigned title_number;
    unsigned track_count;
    unsigned pts_length;

    struct {
        struct {
            unsigned index;
            unsigned length;
        } PTS;

        struct {
            unsigned first;
            unsigned last;
        } sector;
    } tracks[256];
};

struct DVDA_Track_s {
    struct disc_path disc;

    unsigned titleset_number;
    unsigned title_number;
    unsigned track_number;

    struct {
        unsigned index;
        unsigned length;
    } PTS;

    struct {
        unsigned first;
        unsigned last;
    } sector;
};

struct PCM_Track_Reader {
    uint64_t total_pcm_frames;
    uint64_t remaining_pcm_frames;
    PCMDecoder* decoder;
};

struct MLP_Track_Reader {
    unsigned last_sector;
    MLPDecoder* decoder;
};

struct DVDA_Track_Reader_s {
    Packet_Reader* packet_reader;

    dvda_codec_t codec;

    int stream_finished;

    struct stream_parameters parameters;

    union {
        struct PCM_Track_Reader pcm;
        struct MLP_Track_Reader mlp;
    } reader;

    aa_int* channel_data;

    unsigned
    (*decode)(struct DVDA_Track_Reader_s* self, aa_int* samples);

    void
    (*close)(struct DVDA_Track_Reader_s* self);
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

/*given a BitstreamReader to an ATS_XX_0.IFO file
  parses the contents to an atx_XX_0_ifo struct
  returns 1 on success, 0 on failure

  a parsed ats_XX_0_ifo should be freed with free_ats_XX_0_ifo*/
static int
parse_ats_XX_0_ifo(BitstreamReader* reader, struct ats_XX_0_ifo *ifo);

static void
free_ats_XX_0_ifo(struct ats_XX_0_ifo *ifo);

/*given a BitstreamReader positioned at the start of a title
  in the ATS_XX_0.IFO file, parses the data and populates "title"*/
static void
parse_ats_XX_0_ifo_title(BitstreamReader* reader,
                         unsigned table_offset,
                         struct ats_XX_0_ifo_title *title);


/******** PCM-based reader and methods ********/

/*packet reader is the stream of additional packets

  audio_packet is all the data in the packet after
  the pad 2 size value once the stream has been probed

  PTS length is the total length of the track from the index

  pad 2 size is pulled from the packet header

  returns a completed reader which must be closed
  with dvda_close_track_reader()*/
static DVDA_Track_Reader*
open_pcm_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned pts_length,
                      unsigned pad_2_size);

/*samples is a buffer to place decoded samples

  returns the aount of PCM frames read*/
static unsigned
decode_pcm_audio(DVDA_Track_Reader* self, aa_int* samples);

static void
close_pcm_track_reader(DVDA_Track_Reader *reader);


/******** MLP-based reader and methods ********/

/*packet reader is the stream of additional packets

  audio_packet is all the data in the packet after
  the pad 2 size value once the stream has been probed

  PTS length is the total length of the track from the index

  pad 2 size is pulled from the packet header

  returns a completed reader which must be closed
  with dvda_close_track_reader()*/
static DVDA_Track_Reader*
open_mlp_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned last_sector,
                      unsigned pad_2_size);

/*samples is a buffer to place decoded samples

  returns the aount of PCM frames read*/
static unsigned
decode_mlp_audio(DVDA_Track_Reader* self, aa_int* samples);

static void
close_mlp_track_reader(DVDA_Track_Reader *reader);

/*reads the header of an audio packet following the 48 bit packet header*/
static void
read_audio_packet_header(BitstreamReader* packet_reader,
                         unsigned *codec_id,
                         unsigned *pad_2_size);

/*given a buffer of MLP data,
  advances the stream to the start of the next MLP frame
  by finding its major sync and increments bytes_skipped by the number
  of bytes advanced

  returns 1 if a major sync is found, 0 if not*/
static int
find_major_sync(BitstreamReader* mlp_data, unsigned *bytes_skipped);

/*reads the next audio packet of MLP data from packet_reader
  and enqueues its data to mlp_data

  returns 1 on success, 0 on failure*/
static int
enqueue_mlp_packet(Packet_Reader* packet_reader, BitstreamQueue* mlp_data);

/*given a packet reader and packet data (not including header or pad 2 block)
  locates the first set of stream parameters
  and dumps the remaining MLP data in mlp_data
  for later processing

  returns the number of bytes skipped to reach the parameters*/
static unsigned
locate_mlp_parameters(Packet_Reader* packet_reader,
                      BitstreamReader* packet_data,
                      struct stream_parameters* parameters,
                      BitstreamQueue* mlp_data);

/*given a packet reader and initial packet data
  (not including 48 bit header)
  enqueues all MLP data on the stream to mlp_data
  and returns the amount of data queued*/
static unsigned
mlp_data_to_major_sync(Packet_Reader* packet_reader,
                       BitstreamReader* packet_data,
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
    int parsed_ok;

    snprintf(ats_xx_ifo_name, 13, "ATS_%2.2d_0.IFO", MIN(titleset_num, 99));

    if ((ats_xx_ifo_path = find_audio_ts_file(dvda->disc.audio_ts,
                                              ats_xx_ifo_name)) == NULL) {
        /*unable to find requested .IFO file*/
        return NULL;
    }

    ats_xx_ifo = fopen(ats_xx_ifo_path, "rb");

    free(ats_xx_ifo_path);

    if (!ats_xx_ifo) {
        return NULL;
    }

    titleset = malloc(sizeof(DVDA_Titleset));

    disc_path_copy(&dvda->disc, &titleset->disc);

    titleset->titleset_number = titleset_num;

    bs = br_open(ats_xx_ifo, BS_BIG_ENDIAN);

    parsed_ok = parse_ats_XX_0_ifo(bs, &titleset->ifo);

    bs->close(bs);

    if (!parsed_ok) {
        disc_path_free(&titleset->disc);
        free(titleset);
        return NULL;
    }

    return titleset;
}

void
dvda_close_titleset(DVDA_Titleset* titleset)
{
    disc_path_free(&titleset->disc);

    free_ats_XX_0_ifo(&titleset->ifo);

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
    return titleset->ifo.title_count;
}

DVDA_Title*
dvda_open_title(DVDA_Titleset* titleset, unsigned title_num)
{
    DVDA_Title* title;
    struct ats_XX_0_ifo_title *ifo_title;
    unsigned i;

    if ((title_num == 0) || (title_num > titleset->ifo.title_count)) {
        return NULL;
    }

    ifo_title = &(titleset->ifo.title[title_num - 1]);

    title = malloc(sizeof(DVDA_Title));

    disc_path_copy(&titleset->disc, &title->disc);

    title->titleset_number = titleset->titleset_number;
    title->title_number = title_num;
    title->track_count = ifo_title->track_count;
    title->pts_length = ifo_title->PTS_length;

    for (i = 0; i < ifo_title->track_count; i++) {
        struct ats_XX_0_ifo_track *track =
            &ifo_title->track[i];
        struct ats_XX_0_ifo_index *index =
            &ifo_title->index[track->index_number - 1];
        const unsigned track_num = i + 1;
        const int last_track = (track_num == ifo_title->track_count);

        title->tracks[i].PTS.index = track->PTS.index;
        title->tracks[i].PTS.length = track->PTS.length;
        title->tracks[i].sector.first = index->first_sector;
        if (last_track) {
            const int last_title = (title_num == titleset->ifo.title_count);
            if (last_title) {
                /*may need to count sectors in AOBs*/
                title->tracks[i].sector.last = index->last_sector;
            } else {
                struct ats_XX_0_ifo_title *next_title =
                    &(titleset->ifo.title[title_num]);
                if (next_title->track_count) {
                    struct ats_XX_0_ifo_track *next_track =
                        &next_title->track[0];
                    struct ats_XX_0_ifo_index *next_index =
                        &next_title->index[next_track->index_number - 1];

                    title->tracks[i].sector.last = next_index->first_sector - 1;
                } else {
                    /*next title has no tracks - this shouldn't happen*/
                    title->tracks[i].sector.last = index->last_sector;
                }
            }
        } else {
            struct ats_XX_0_ifo_track *next_track =
                &ifo_title->track[i + 1];
            struct ats_XX_0_ifo_index *next_index =
                &ifo_title->index[next_track->index_number - 1];

            title->tracks[i].sector.last = next_index->first_sector - 1;
        }
    }

    return title;
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
        /*track_num out of range*/
        return NULL;
    }

    track = malloc(sizeof(DVDA_Track));

    disc_path_copy(&title->disc, &track->disc);

    track->titleset_number = title->titleset_number;
    track->title_number = title->title_number;
    track->track_number = track_num;

    /*converted 1-based track_num to 0-based track_num*/
    track_num -= 1;

    track->PTS.index = title->tracks[track_num].PTS.index;
    track->PTS.length = title->tracks[track_num].PTS.length;
    track->sector.first = title->tracks[track_num].sector.first;
    track->sector.last = title->tracks[track_num].sector.last;

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
    return track->PTS.index;
}

unsigned
dvda_track_pts_length(const DVDA_Track* track)
{
    return track->PTS.length;
}

unsigned
dvda_track_first_sector(const DVDA_Track* track)
{
    return track->sector.first;
}

unsigned
dvda_track_last_sector(const DVDA_Track* track)
{
    return track->sector.last;
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
    unsigned sector;

    /*open an AOB reader for the given disc*/
    if ((aob_reader = aob_reader_open(track->disc.audio_ts,
                                      track->disc.device,
                                      track->titleset_number)) == NULL) {
        return NULL;
    }

    /*seek to the track's first sector*/
    if (aob_reader_seek(aob_reader, track->sector.first)) {
        aob_reader_close(aob_reader);
        return NULL;
    }

    /*wrap AOB reader with packet reader*/
    packet_reader = packet_reader_open(aob_reader);

    /*get first audio packet from packet reader*/
    audio_packet = packet_reader_next_audio_packet(packet_reader, &sector);

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
                                             track->PTS.length,
                                             pad_2_size);
        break;
    case MLP_CODEC_ID:
        track_reader = open_mlp_track_reader(packet_reader,
                                             audio_packet,
                                             track->sector.last,
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
    reader->close(reader);
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
    const unsigned channel_count = dvda_channel_count(reader);
    aa_int* channel_data = reader->channel_data;
    unsigned amount_read;
    unsigned c;

    if (!pcm_frames) {
        return 0;
    }

    /*populate per-channel buffer with samples as needed*/
    if (!reader->stream_finished) {
        while (channel_data->_[0]->len < pcm_frames) {
            const unsigned pcm_frames_read =
                reader->decode(reader, channel_data);
            if (!pcm_frames_read) {
                /*no more data in stream*/
                reader->stream_finished = 1;
                break;
            }
        }
    }

    amount_read = MIN(pcm_frames, channel_data->_[0]->len);

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

static int
parse_ats_XX_0_ifo(BitstreamReader* bs, struct ats_XX_0_ifo *ifo)
{
    ifo->title = NULL;

    if (!setjmp(*br_try(bs))) {
        uint8_t identifier[12];
        const uint8_t dvdaudio_ats[12] =
            {68, 86, 68, 65, 85, 68, 73, 79, 45, 65, 84, 83};
        unsigned i;

        bs->read_bytes(bs, identifier, 12);

        if (memcmp(identifier, dvdaudio_ats, 12)) {
            /*some identifier mismatch*/
            br_abort(bs);
        }

        bs->seek(bs, SECTOR_SIZE, BS_SEEK_SET);

        bs->parse(bs, "16u 16p 32p", &ifo->title_count);

        ifo->title = malloc(ifo->title_count *
                            sizeof(struct ats_XX_0_ifo_title));

        for (i = 0; i < ifo->title_count; i++) {
            unsigned title_number;
            unsigned title_table_offset;
            br_pos_t *current_pos;

            bs->parse(bs, "8u 24p 32u", &title_number, &title_table_offset);
            current_pos = bs->getpos(bs);
            bs->seek(bs, SECTOR_SIZE + title_table_offset, BS_SEEK_SET);
            parse_ats_XX_0_ifo_title(bs,
                                     title_table_offset,
                                     &ifo->title[i]);
            bs->setpos(bs, current_pos);
            current_pos->del(current_pos);
        }

        br_etry(bs);
        return 1;
    } else {
        br_etry(bs);
        free(ifo->title);
        fprintf(stderr, "I/O error\n");
        return 0;
    }
}

static void
free_ats_XX_0_ifo(struct ats_XX_0_ifo *ifo)
{
    free(ifo->title);
}

static void
parse_ats_XX_0_ifo_title(BitstreamReader* reader,
                         unsigned table_offset,
                         struct ats_XX_0_ifo_title *title)
{
    unsigned i;
    unsigned sector_pointers_offset;

    reader->parse(reader, "16p 8u 8u 32u 32p 16u 16p",
                  &title->track_count,
                  &title->index_count,
                  &title->PTS_length,
                  &sector_pointers_offset);

    /*populate tracks*/
    for (i = 0; i < title->track_count; i++) {
        reader->parse(reader, "32p 8u 8p 32u 32u 48p",
                      &title->track[i].index_number,
                      &title->track[i].PTS.index,
                      &title->track[i].PTS.length);
    }

    /*populate indexes*/
    reader->seek(reader,
                 SECTOR_SIZE + table_offset + sector_pointers_offset,
                 BS_SEEK_SET);
    for (i = 0; i < title->index_count; i++) {
        unsigned index_id;

        reader->parse(reader, "32u 32u 32u",
                      &index_id,
                      &title->index[i].first_sector,
                      &title->index[i].last_sector);
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
    uint64_t total_pcm_frames;
    unsigned pcm_frames_read;

    DVDA_Track_Reader* track_reader = malloc(sizeof(DVDA_Track_Reader));
    track_reader->packet_reader = packet_reader;

    /*FIXME - check for I/O errors?*/

    /*pull stream attributes from start of packet*/
    /*it appears PCM data always starts at the beginning of the
      track's first sector and PCM data doesn't cross packet boundaries*/
    track_reader->codec = DVDA_PCM;
    track_reader->stream_finished = 0;
    dvda_pcmdecoder_decode_params(audio_packet, &(track_reader->parameters));

    pts_length_d *= unpack_sample_rate(track_reader->parameters.group_0_rate);
    pts_length_d /= PTS_PER_SECOND;

    total_pcm_frames = lround(pts_length_d);

    track_reader->reader.pcm.total_pcm_frames =
        track_reader->reader.pcm.remaining_pcm_frames =
        total_pcm_frames;

    channel_count =
        unpack_channel_count(track_reader->parameters.channel_assignment);

    track_reader->reader.pcm.decoder = dvda_open_pcmdecoder(
        unpack_bits_per_sample(track_reader->parameters.group_0_bps),
        channel_count);

    /*setup initial channels*/
    track_reader->channel_data = aa_int_new();
    for (c = 0; c < channel_count; c++) {
        (void)track_reader->channel_data->append(track_reader->channel_data);
    }

    /*decode remaining bytes in packet to buffer*/
    audio_packet->skip_bytes(audio_packet, pad_2_size - 9);

    pcm_frames_read =
        dvda_pcmdecoder_decode_packet(track_reader->reader.pcm.decoder,
                                      audio_packet,
                                      track_reader->channel_data);

    track_reader->reader.pcm.remaining_pcm_frames -=
        MIN(pcm_frames_read, total_pcm_frames);

    /*setup reader's methods*/
    track_reader->decode = decode_pcm_audio;
    track_reader->close = close_pcm_track_reader;

    return track_reader;
}

static unsigned
decode_pcm_audio(DVDA_Track_Reader* self, aa_int* samples)
{
    BitstreamReader* packet;
    unsigned sector;

    if (!self->reader.pcm.remaining_pcm_frames) {
        /*no more data to output*/
        return 0;
    }

    packet = packet_reader_next_audio_packet(self->packet_reader, &sector);

    if (!packet) {
        /*no more packets in stream*/
        return 0;
    }

    if (!setjmp(*br_try(packet))) {
        unsigned codec_id;
        unsigned pad_2_size;
        struct stream_parameters parameters;
        unsigned pcm_frames_read;

        read_audio_packet_header(packet, &codec_id, &pad_2_size);

        if (codec_id != PCM_CODEC_ID) {
            /*codec mismatch in stream*/
            br_etry(packet);
            packet->close(packet);
            return 0;
        }

        dvda_pcmdecoder_decode_params(packet, &parameters);

        if (!dvda_params_equal(&self->parameters, &parameters)) {
            /*stream parameters mismatch*/
            br_etry(packet);
            packet->close(packet);
            return 0;
        }

        packet->skip_bytes(packet, pad_2_size - 9);

        pcm_frames_read =
            dvda_pcmdecoder_decode_packet(self->reader.pcm.decoder,
                                          packet,
                                          samples);

        br_etry(packet);
        packet->close(packet);

        /*FIXME - handle the case of PCM frames not falling
          on packet boundaries?*/
        self->reader.pcm.remaining_pcm_frames -=
            MIN(pcm_frames_read,
                self->reader.pcm.remaining_pcm_frames);

        /*return all samples*/
        return pcm_frames_read;
    } else {
        /*some I/O error reading packet*/
        br_etry(packet);
        packet->close(packet);
        return 0;
    }
}

static void
close_pcm_track_reader(DVDA_Track_Reader *reader)
{
    packet_reader_close(reader->packet_reader);
    dvda_close_pcmdecoder(reader->reader.pcm.decoder);
    reader->channel_data->del(reader->channel_data);
    free(reader);
}


static DVDA_Track_Reader*
open_mlp_track_reader(Packet_Reader* packet_reader,
                      BitstreamReader* audio_packet,
                      unsigned last_sector,
                      unsigned pad_2_size)
{
    unsigned channel_count;
    unsigned c;
    BitstreamQueue* mlp_data;

    DVDA_Track_Reader* track_reader = malloc(sizeof(DVDA_Track_Reader));
    track_reader->packet_reader = packet_reader;

    track_reader->codec = DVDA_MLP;
    track_reader->stream_finished = 0;

    /*FIXME - check for I/O errors?*/

    /*skip initial padding*/
    audio_packet->skip_bytes(audio_packet, pad_2_size);

    mlp_data = br_open_queue(BS_BIG_ENDIAN);

    /*FIXME - save offset somewhere?*/
    locate_mlp_parameters(packet_reader,
                          audio_packet,
                          &track_reader->parameters,
                          mlp_data);

    channel_count =
        unpack_channel_count(track_reader->parameters.channel_assignment);

    track_reader->reader.mlp.last_sector = last_sector;
    track_reader->reader.mlp.decoder =
        dvda_open_mlpdecoder(&(track_reader->parameters));

    /*setup initial channels*/
    track_reader->channel_data = aa_int_new();
    for (c = 0; c < channel_count; c++) {
        (void)track_reader->channel_data->append(track_reader->channel_data);
    }

    /*decode remaining bytes in packet to buffer*/
    /*decode remaining MLP frames in packet to buffer*/
    dvda_mlpdecoder_decode_packet(track_reader->reader.mlp.decoder,
                                  (BitstreamReader*)mlp_data,
                                  track_reader->channel_data);

    mlp_data->close(mlp_data);

    /*setup reader's methods*/
    track_reader->decode = decode_mlp_audio;
    track_reader->close = close_mlp_track_reader;

    return track_reader;
}

static unsigned
decode_mlp_audio(DVDA_Track_Reader* self, aa_int* samples)
{
    BitstreamReader* packet;
    unsigned sector;

    if (self->stream_finished) {
        return 0;
    }

    packet = packet_reader_next_audio_packet(self->packet_reader, &sector);

    if (!packet) {
        return 0;
    }

    /*if the current sector is outside the track's range of sectors*/
    /*process only until the next major sync*/
    if (sector > self->reader.mlp.last_sector) {
        BitstreamQueue* mlp_data = br_open_queue(BS_BIG_ENDIAN);
        unsigned extra_bytes = mlp_data_to_major_sync(self->packet_reader,
                                                      packet,
                                                      mlp_data);
        unsigned pcm_frames_read;

        packet->close(packet);

        if (extra_bytes) {
            assert(extra_bytes == mlp_data->size(mlp_data));

            pcm_frames_read =
                dvda_mlpdecoder_decode_packet(self->reader.mlp.decoder,
                                              (BitstreamReader*)mlp_data,
                                              samples);
        } else {
            pcm_frames_read = 0;
        }

        mlp_data->close(mlp_data);

        self->stream_finished = 1;

        return pcm_frames_read;
    }

    if (!setjmp(*br_try(packet))) {
        unsigned codec_id;
        unsigned pad_2_size;
        unsigned pcm_frames_read;

        read_audio_packet_header(packet, &codec_id, &pad_2_size);

        if (codec_id != MLP_CODEC_ID) {
            /*codec mismatch in stream*/
            br_etry(packet);
            packet->close(packet);
            return 0;
        }

        packet->skip_bytes(packet, pad_2_size);

        pcm_frames_read =
            dvda_mlpdecoder_decode_packet(self->reader.mlp.decoder,
                                          packet,
                                          samples);

        br_etry(packet);
        packet->close(packet);

        return pcm_frames_read;
    } else {
        /*I/O error reading packet*/
        br_etry(packet);
        packet->close(packet);
        return 0;
    }
}

static void
close_mlp_track_reader(DVDA_Track_Reader *reader)
{
    packet_reader_close(reader->packet_reader);
    dvda_close_mlpdecoder(reader->reader.mlp.decoder);
    reader->channel_data->del(reader->channel_data);
    free(reader);
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

static int
find_major_sync(BitstreamReader* mlp_data, unsigned *bytes_skipped)
{
    while (mlp_data->size(mlp_data) >= 8) {
        unsigned sync_words;
        unsigned stream_type;
        br_pos_t* mlp_frame_start;

        /*look for major sync*/
        mlp_frame_start = mlp_data->getpos(mlp_data);

        mlp_data->parse(mlp_data, "4p 12p 16p 24u 8u",
                        &sync_words, &stream_type);

        /*if major sync is found*/
        if ((sync_words == 0xF8726F) && (stream_type == 0xBB)) {
            /*rewind to start of frame and return success*/
            mlp_data->setpos(mlp_data, mlp_frame_start);
            mlp_frame_start->del(mlp_frame_start);

            return 1;
        } else {  /*if major sync is not found*/
            /*rewind to start of data*/
            mlp_data->setpos(mlp_data, mlp_frame_start);
            mlp_frame_start->del(mlp_frame_start);

            /*advance 1 byte*/
            mlp_data->skip(mlp_data, 8);

            /*and continue looking*/
            *bytes_skipped += 1;
        }
    }

    /*not enough data to contain a major sync*/
    return 0;
}

static int
enqueue_mlp_packet(Packet_Reader* packet_reader, BitstreamQueue* mlp_data)
{
    unsigned sector;
    BitstreamReader* packet =
        packet_reader_next_audio_packet(packet_reader, &sector);
    unsigned codec_id;
    unsigned pad_2_size;

    if (!packet) {
        return 0;
    }

    read_audio_packet_header(packet, &codec_id, &pad_2_size);

    if (codec_id != MLP_CODEC_ID) {
        packet->close(packet);
        return enqueue_mlp_packet(packet_reader, mlp_data);
    }

    packet->skip_bytes(packet, pad_2_size);

    packet->enqueue(packet,
                    packet->size(packet),
                    mlp_data);

    packet->close(packet);
    return 1;
}

static unsigned
locate_mlp_parameters(Packet_Reader* packet_reader,
                      BitstreamReader* packet_data,
                      struct stream_parameters* parameters,
                      BitstreamQueue* mlp_data)
{
    unsigned bytes_skipped = 0;
    BitstreamReader* mlp_reader = (BitstreamReader*)mlp_data;
    br_pos_t* mlp_frame_start;

    packet_data->enqueue(packet_data,
                         packet_data->size(packet_data),
                         mlp_data);

    /*while no major sync is found*/
    while (!find_major_sync(mlp_reader, &bytes_skipped)) {
        if (!enqueue_mlp_packet(packet_reader, mlp_data)) {
            /*FIXME - ran out of additional packets*/
            assert(0);
            break;
        }
    }

    while (mlp_data->size(mlp_data) < 18) {
        if (!enqueue_mlp_packet(packet_reader, mlp_data)) {
            /*FIXME - ran out of additional packets*/
            assert(0);
            break;
        }
    }

    /*finally, grab stream data after major sync*/
    mlp_frame_start = mlp_reader->getpos(mlp_reader);
    mlp_reader->parse(mlp_reader,
                      "4p 12p 16p" /*total frame size (* 2)*/
                      "24p 8p"     /*sync words, stream type*/
                      "4u 4u 4u 4u 11p 5u 48p",
                      &parameters->group_0_bps,
                      &parameters->group_1_bps,
                      &parameters->group_0_rate,
                      &parameters->group_1_rate,
                      &parameters->channel_assignment);
    mlp_reader->setpos(mlp_reader, mlp_frame_start);
    mlp_frame_start->del(mlp_frame_start);

    /*return amount of bytes skipped*/
    return bytes_skipped;
}

static unsigned
mlp_data_to_major_sync(Packet_Reader* packet_reader,
                       BitstreamReader* packet_data,
                       BitstreamQueue* mlp_data)
{
    BitstreamQueue* packet_queue = br_open_queue(BS_BIG_ENDIAN);
    br_pos_t* queue_start =
        packet_queue->getpos((BitstreamReader*)packet_queue);
    unsigned bytes_queued = 0;

    unsigned codec_id;
    unsigned pad_2_size;

    /*FIXME - handle read errors*/

    /*populate queue with initial packet data*/
    read_audio_packet_header(packet_data, &codec_id, &pad_2_size);

    if (codec_id != MLP_CODEC_ID) {
        /*codec mismatch in stream*/
        packet_queue->close(packet_queue);
        queue_start->del(queue_start);
        return 0;
    }

    packet_data->skip_bytes(packet_data, pad_2_size);

    packet_data->enqueue(packet_data,
                         packet_data->size(packet_data),
                         packet_queue);

    /*while no major sync is found*/
    while (!find_major_sync((BitstreamReader*)packet_queue, &bytes_queued)) {
        /*continue populating queue with packet data*/
        if (!enqueue_mlp_packet(packet_reader, packet_queue)) {
            /*FIXME - ran out of additional MLP packets*/
            assert(0);
            break;
        }
    }

    /*finally, push only data from stream start to major sync to mlp_data*/
    packet_queue->setpos((BitstreamReader*)packet_queue, queue_start);

    queue_start->del(queue_start);

    packet_queue->enqueue((BitstreamReader*)packet_queue,
                          bytes_queued,
                          mlp_data);

    packet_queue->close(packet_queue);

    /*then return amount actually queued*/
    return bytes_queued;
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
