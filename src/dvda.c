#include "dvda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "bitstream.h"
#include "array.h"
#include "audio_ts.h"
#include "aob.h"

#define SECTOR_SIZE 2048

struct DVDA_s {
    char *audio_ts_path;
    char *device;
    unsigned titleset_count;
};

struct DVDA_Titleset_s {
    char *ats_xx_ifo_path;
    unsigned title_count;
};

struct DVDA_Track_s {
    unsigned index_number;
    unsigned pts_index;
    unsigned pts_length;
};

struct DVDA_Index_s {
    unsigned first_sector;
    unsigned last_sector;
};

struct DVDA_Title_s {
    unsigned title_number;
    unsigned track_count;
    unsigned index_count;
    unsigned pts_length;
    DVDA_Track tracks[256];
    DVDA_Index indexes[256];
};

struct DVDA_Titleset_Reader_s {
    char *audio_ts_path;
    char *device;
    unsigned titleset;
};

struct DVDA_Title_Reader_s {
    AOB_Reader* aob_reader;
    dvda_codec_t codec;
    unsigned bits_per_sample;
    unsigned sample_rate;
    unsigned channel_count;
    unsigned channel_assignment;
};

/*******************************************************************
 *                   private function definitions                  *
 *******************************************************************/

/*given a full path to the AUDIO_TS.IFO file
  returns the disc's title set count
  or 0 if an error occurs opening or parsing the file*/
static unsigned
get_titleset_count(const char *audio_ts_ifo);

/*returns 0 on success, 1 on failure*/
static int
dvda_probe_stream(const uint8_t* sector_data,
                  dvda_codec_t* codec,
                  unsigned* bits_per_sample,
                  unsigned* sample_rate,
                  unsigned* channel_count,
                  unsigned* channel_assignment);

static int
dvda_probe_pcm(BitstreamReader *sector_reader,
               unsigned pad_2_size,
               unsigned* bits_per_sample,
               unsigned* sample_rate,
               unsigned* channel_count,
               unsigned* channel_assignment);

static int
dvda_probe_mlp(BitstreamReader *sector_reader,
               unsigned pad_2_size,
               unsigned* bits_per_sample,
               unsigned* sample_rate,
               unsigned* channel_count,
               unsigned* channel_assignment);

/*readers 0 on success, 1 on failure*/
static int
read_pack_header(BitstreamReader *sector_reader,
                 uint64_t *pts,
                 unsigned *SCR_extension,
                 unsigned *bitrate);

/*reads the 48 bit packet header
  if start code is 1,
  populates stream_id and packet_length and returns 0
  otherwise returns 1*/
int
read_packet_header(BitstreamReader* sector_reader,
                   unsigned *stream_id,
                   unsigned *packet_length);

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
    dvda->audio_ts_path = strdup(audio_ts_path);
    dvda->device = device ? strdup(device) : NULL;
    dvda->titleset_count = titleset_count;
    return dvda;
}

void
dvda_close(DVDA *dvda)
{
    free(dvda->audio_ts_path);
    free(dvda->device);
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

    if ((ats_xx_ifo_path = find_audio_ts_file(dvda->audio_ts_path,
                                              ats_xx_ifo_name)) == NULL) {
        /*unable to find requested .IFO file*/
        return NULL;
    }

    ats_xx_ifo = fopen(ats_xx_ifo_path, "rb");
    if (ats_xx_ifo) {
        titleset = malloc(sizeof(DVDA_Titleset));
        titleset->ats_xx_ifo_path = ats_xx_ifo_path;
        titleset->title_count = 0;
    } else {
        free(ats_xx_ifo_path);
        return NULL;
    }
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
    free(titleset->ats_xx_ifo_path);
    free(titleset);
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

    if (ats_xx_ifo) {
        bs = br_open(ats_xx_ifo, BS_BIG_ENDIAN);
        title = malloc(sizeof(DVDA_Title));
        title->title_number = 0;
        title->track_count = 0;
        title->index_count = 0;
        title->pts_length = 0;
    } else {
        return NULL;
    }

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
    free(title);
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

DVDA_Title_Reader*
dvda_open_title_reader(DVDA* dvda, unsigned titleset, DVDA_Title* title)
{
    AOB_Reader* aob_reader;
    DVDA_Track* first_track;
    unsigned first_sector_position;
    uint8_t sector_data[SECTOR_SIZE];
    dvda_codec_t codec;
    unsigned bits_per_sample;
    unsigned sample_rate;
    unsigned channel_count;
    unsigned channel_mask;
    DVDA_Title_Reader* title_reader;

    /*open an AOB reader for the given title set*/
    if ((aob_reader = aob_reader_open(dvda->audio_ts_path,
                                      dvda->device,
                                      titleset)) == NULL) {
        return NULL;
    }

    /*get the first sector of the first track track for the given title*/
    if ((first_track = dvda_open_track(title, 1)) != NULL) {
        first_sector_position = dvda_track_first_sector(title, first_track);
        dvda_close_track(first_track);
    } else{
        aob_reader_close(aob_reader);
        return NULL;
    }

    /*attempt to seek to the beginning of the title's first track*/
    if (aob_reader_seek(aob_reader, first_sector_position)) {
        aob_reader_close(aob_reader);
        return NULL;
    }

    /*probe stream for initial values*/
    aob_reader_read(aob_reader, sector_data);
    if (dvda_probe_stream(sector_data,
                          &codec,
                          &bits_per_sample,
                          &sample_rate,
                          &channel_count,
                          &channel_mask)) {
        aob_reader_close(aob_reader);
        return NULL;
    }

    /*reposition reader at start of title's first track*/
    aob_reader_seek(aob_reader, first_sector_position);

    /*build populated title reader and return it*/
    title_reader = malloc(sizeof(DVDA_Title_Reader));
    title_reader->aob_reader = aob_reader;

    title_reader->codec = codec;
    title_reader->bits_per_sample = bits_per_sample;
    title_reader->sample_rate = sample_rate;
    title_reader->channel_count = channel_count;
    title_reader->channel_assignment = channel_mask;

    return title_reader;
}

void
dvda_close_title_reader(DVDA_Title_Reader* title_reader)
{
    aob_reader_close(title_reader->aob_reader);
    free(title_reader);
}

dvda_codec_t
dvda_codec(DVDA_Title_Reader* reader)
{
    return reader->codec;
}

unsigned
dvda_bits_per_sample(DVDA_Title_Reader* reader)
{
    return reader->bits_per_sample;
}

unsigned
dvda_sample_rate(DVDA_Title_Reader* reader)
{
    return reader->sample_rate;
}

unsigned
dvda_channel_count(DVDA_Title_Reader* reader)
{
    return reader->channel_count;
}

unsigned
dvda_channel_assignment(DVDA_Title_Reader* reader)
{
    return reader->channel_assignment;
}

unsigned
dvda_read(DVDA_Title_Reader* reader,
          unsigned pcm_frames,
          int buffer[]);

DVDA_Track*
dvda_open_track(DVDA_Title* title, unsigned track_num)
{
    DVDA_Track* track;

    if ((track_num == 0) || (track_num > title->track_count)) {
        return NULL;
    } else {
        /*change 1-based index to 0-based index*/
        track_num--;
    }

    track = malloc(sizeof(DVDA_Track));
    track->index_number = title->tracks[track_num].index_number;
    track->pts_index = title->tracks[track_num].pts_index;
    track->pts_length = title->tracks[track_num].pts_length;
    return track;
}

void
dvda_close_track(DVDA_Track* track)
{
    free(track);
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
dvda_track_first_sector(const DVDA_Title* title,
                        const DVDA_Track* track)
{
    return title->indexes[track->index_number - 1].first_sector;
}

unsigned
dvda_track_last_sector(const DVDA_Title* title,
                       const DVDA_Track* track)
{
    return title->indexes[track->index_number - 1].last_sector;
}

/*******************************************************************
 *                  private function implementations               *
 *******************************************************************/

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
dvda_probe_stream(const uint8_t* sector_data,
                  dvda_codec_t* codec,
                  unsigned* bits_per_sample,
                  unsigned* sample_rate,
                  unsigned* channel_count,
                  unsigned* channel_assignment)
{
    /*turn our blob of data into a BitstreamReader*/
    BitstreamQueue* sector_reader =
        br_open_queue_populated(sector_data, SECTOR_SIZE, BS_BIG_ENDIAN);
    BitstreamReader* reader = (BitstreamReader*)sector_reader;
    uint64_t pts;
    unsigned SCR_extension;
    unsigned bitrate;

    /*skip over the variable-sized pack header*/
    if (read_pack_header(reader, &pts, &SCR_extension, &bitrate)) {
        sector_reader->close(sector_reader);
        return 1;
    }

    /*look for the first audio packet in the sector*/
    if (!setjmp(*br_try(reader))) {
        while (sector_reader->size(sector_reader)) {
            unsigned stream_id;
            unsigned packet_length;
            if (read_packet_header(reader, &stream_id, &packet_length)) {
                br_abort(reader);
            }
            if (stream_id == 0xBD) {
                unsigned pad_1_size;
                unsigned codec_id;
                unsigned pad_2_size;

                reader->parse(reader, "16p 8u", &pad_1_size);
                reader->skip_bytes(reader, pad_1_size);
                reader->parse(reader, "8u 8p 8p 8u", &codec_id, &pad_2_size);

                switch (codec_id) {
                case 0xA0:
                    /*if codec is PCM
                      extract attributes from packet's PCM header*/
                    {
                        const int result = dvda_probe_pcm(
                            reader,
                            pad_2_size,
                            bits_per_sample,
                            sample_rate,
                            channel_count,
                            channel_assignment);
                        br_etry(reader);
                        sector_reader->close(sector_reader);
                        *codec = DVDA_PCM;
                        return result;
                    }
                case 0xA1:
                    /*if codec is MLP
                      extract attributes from initial MLP frame*/
                    {
                        const int result = dvda_probe_mlp(
                            reader,
                            pad_2_size,
                            bits_per_sample,
                            sample_rate,
                            channel_count,
                            channel_assignment);
                        br_etry(reader);
                        sector_reader->close(sector_reader);
                        *codec = DVDA_MLP;
                        return result;
                    }
                default:
                    /*if codec is unknown, return an error*/
                    br_abort(reader);
                }
            } else {
                /*ignore non-audio packets*/
                reader->skip_bytes(reader, packet_length);
            }
        }

        /*if no audio packets found, return an error*/
        br_etry(reader);
        sector_reader->close(sector_reader);
        return 1;
    } else {
        br_etry(reader);
        sector_reader->close(sector_reader);
        return 1;
    }
}

static int
dvda_probe_pcm(BitstreamReader *sector_reader,
               unsigned pad_2_size,
               unsigned* bits_per_sample,
               unsigned* sample_rate,
               unsigned* channel_count,
               unsigned* channel_assignment)
{
    unsigned first_audio_frame;
    unsigned group_0_bps;
    unsigned group_1_bps;
    unsigned group_0_rate;
    unsigned group_1_rate;
    unsigned crc;

    sector_reader->parse(
        sector_reader,
        "16u 8p 4u 4u 4u 4u 8p 8u 8p 8u",
        &first_audio_frame,
        &group_0_bps,
        &group_1_bps,
        &group_0_rate,
        &group_1_rate,
        channel_assignment,
        &crc);

    *bits_per_sample = unpack_bits_per_sample(group_0_bps);
    *sample_rate = unpack_sample_rate(group_0_rate);
    *channel_count = unpack_channel_count(*channel_assignment);

    return 0;
}

static int
dvda_probe_mlp(BitstreamReader *sector_reader,
               unsigned pad_2_size,
               unsigned* bits_per_sample,
               unsigned* sample_rate,
               unsigned* channel_count,
               unsigned* channel_assignment)
{
    unsigned total_frame_size;
    unsigned sync_words;
    unsigned stream_type;
    unsigned group_0_bps;
    unsigned group_1_bps;
    unsigned group_0_rate;
    unsigned group_1_rate;

    sector_reader->skip_bytes(sector_reader, pad_2_size);
    sector_reader->parse(
        sector_reader,
        "4p 12u 16p 24u 8u 4u 4u 4u 4u 11p 5u",
        &total_frame_size,
        &sync_words,
        &stream_type,
        &group_0_bps,
        &group_1_bps,
        &group_0_rate,
        &group_1_rate,
        channel_assignment);

    if ((sync_words == 0xF8726F) && (stream_type == 0xBB)) {
        *bits_per_sample = unpack_bits_per_sample(group_0_bps);
        *sample_rate = unpack_sample_rate(group_0_rate);
        *channel_count = unpack_channel_count(*channel_assignment);

        return 0;
    } else {
        return 1;
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

int
read_packet_header(BitstreamReader* sector_reader,
                   unsigned *stream_id,
                   unsigned *packet_length)
{
    unsigned start_code;

    sector_reader->parse(sector_reader,
                         "24u 8u 16u",
                         &start_code,
                         stream_id,
                         packet_length);

    return (start_code == 1) ? 0 : 1;
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
    case 0: /*front center*/
        return 1;
    case 1: /*front left, front right*/
        return 2;
    case 2: /*front left, front right, back center*/
    case 4: /*front left, front right, LFE*/
    case 7: /*front left, front right, front center*/
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
