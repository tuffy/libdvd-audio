#include "dvda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bitstream.h"
#include "audio_ts.h"

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
    DVDA_Track_t tracks[256];
    DVDA_Index_t indexes[256];
};

/*******************************************************************
 *                   private function definitions                  *
 *******************************************************************/

/*given a full path to the AUDIO_TS.IFO file
  returns the disc's title set count
  or 0 if an error occurs opening or parsing the file*/
unsigned
get_titleset_count(const char *audio_ts_ifo);

/*******************************************************************
 *                  public function implementations                *
 *******************************************************************/

DVDA_t*
dvda_open(const char *audio_ts_path, const char *device)
{
    DVDA_t *dvda;
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

    dvda = malloc(sizeof(DVDA_t));
    dvda->audio_ts_path = strdup(audio_ts_path);
    dvda->device = device ? strdup(device) : NULL;
    dvda->titleset_count = titleset_count;
    return dvda;
}

void
dvda_close(DVDA_t *dvda)
{
    free(dvda->audio_ts_path);
    free(dvda->device);
    free(dvda);
}

unsigned
dvda_titleset_count(const DVDA_t *dvda)
{
    return dvda->titleset_count;
}

DVDA_Titleset_t*
dvda_open_titleset(DVDA_t* dvda, unsigned titleset_num)
{
    char ats_xx_ifo_name[13];
    char *ats_xx_ifo_path;
    FILE *ats_xx_ifo;
    BitstreamReader *bs;
    DVDA_Titleset_t *titleset;

    snprintf(ats_xx_ifo_name, 13, "ATS_%2.2d_0.IFO", MIN(titleset_num, 99));

    if ((ats_xx_ifo_path = find_audio_ts_file(dvda->audio_ts_path,
                                              ats_xx_ifo_name)) == NULL) {
        /*unable to find requested .IFO file*/
        return NULL;
    }

    ats_xx_ifo = fopen(ats_xx_ifo_path, "rb");
    if (ats_xx_ifo) {
        titleset = malloc(sizeof(DVDA_Titleset_t));
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

        bs->seek(bs, 2048, BS_SEEK_SET);

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
dvda_close_titleset(DVDA_Titleset_t* titleset)
{
    free(titleset->ats_xx_ifo_path);
    free(titleset);
}

unsigned
dvda_title_count(const DVDA_Titleset_t* titleset)
{
    return titleset->title_count;
}

DVDA_Title_t*
dvda_open_title(DVDA_Titleset_t* titleset, unsigned title_num)
{
    FILE *ats_xx_ifo = fopen(titleset->ats_xx_ifo_path, "rb");
    BitstreamReader *bs;
    unsigned title_count;
    unsigned i;
    DVDA_Title_t* title;

    if (ats_xx_ifo) {
        bs = br_open(ats_xx_ifo, BS_BIG_ENDIAN);
        title = malloc(sizeof(DVDA_Title_t));
        title->title_number = 0;
        title->track_count = 0;
        title->index_count = 0;
        title->pts_length = 0;
    } else {
        return NULL;
    }

    if (!setjmp(*br_try(bs))) {
        bs->seek(bs, 2048, BS_SEEK_SET);
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

                bs->seek(bs, 2048 + title_table_offset, BS_SEEK_SET);
                bs->parse(bs, "16p 8u 8u 32u 32p 16u 16p",
                          &title->track_count,
                          &title->index_count,
                          &title->pts_length,
                          &sector_pointers_offset);

                for (i = 0; i < title->track_count; i++) {
                    bs->parse(bs, "32p 8u 8p 32u 32u 48p",
                              &(title->tracks[i].index_number),
                              &(title->tracks[i].pts_index),
                              &(title->tracks[i].pts_length));
                }

                /*FIXME - populate indexes*/

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
dvda_close_title(DVDA_Title_t* title)
{
    free(title);
}

unsigned
dvda_track_count(const DVDA_Title_t* title)
{
    return title->track_count;
}

DVDA_Track_t*
dvda_open_track(DVDA_Title_t* title, unsigned track_num)
{
    DVDA_Track_t* track;

    if ((track_num == 0) || (track_num > title->track_count)) {
        return NULL;
    } else {
        /*change 1-based index to 0-based index*/
        track_num--;
    }

    track = malloc(sizeof(DVDA_Track_t));
    track->index_number = title->tracks[track_num].index_number;
    track->pts_index = title->tracks[track_num].pts_index;
    track->pts_length = title->tracks[track_num].pts_length;
    return track;
}

void
dvda_close_track(DVDA_Track_t* track)
{
    free(track);
}

unsigned
dvda_track_pts_index(const DVDA_Track_t* track)
{
    return track->pts_index;
}

unsigned
dvda_track_pts_length(const DVDA_Track_t* track)
{
    return track->pts_length;
}

/*******************************************************************
 *                  private function implementations               *
 *******************************************************************/

unsigned
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
