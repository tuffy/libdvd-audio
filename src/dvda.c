#include "dvda.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "bitstream.h"

struct DVDA_s {
    char *audio_ts_path;
    char *device;
    unsigned titleset_count;
};

/*a case-insensitive version of strcmp*/
static int
strcmp_insensitive(const char *s1, const char *s2);

/*given a path to the AUDIO_TS directory
  and a filename to search for
  returns the full path to the file
  or NULL if the file is not found
  the path must be freed later once no longer needed

   filenames are compared case-insensitively*/
static char*
find_audio_ts_file(const char* audio_ts_path, const char* filename);

/*given a full path to the AUDIO_TS.IFO file
  returns the disc's title set count
  or 0 if an error occurs opening or parsing the file*/
unsigned
get_titleset_count(const char *audio_ts_ifo);

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

static char*
find_audio_ts_file(const char* audio_ts_path, const char* filename)
{
    DIR* dir = opendir(audio_ts_path);
    struct dirent* dirent;
    if (!dir) {
        return NULL;
    }

    for (dirent = readdir(dir); dirent != NULL; dirent = readdir(dir)) {
        /*convert directory entry to upper-case*/
        if (!strcmp_insensitive(filename, dirent->d_name)) {
            /*if the filename matches,
              join audio_ts path and name into a single path
              and return it*/

            const size_t dirent_len = strlen(dirent->d_name);

            const size_t full_path_len = (strlen(audio_ts_path) +
                                          1 + /*path separator*/
                                          dirent_len +
                                          1 /*NULL*/);
            char* full_path = malloc(full_path_len);

            snprintf(full_path, full_path_len,
                     "%s/%s", audio_ts_path, dirent->d_name);

            closedir(dir);
            return full_path;
        }
    }

    /*gone through entire directory without a match*/
    closedir(dir);
    return NULL;
}

static int
strcmp_insensitive(const char *s, const char *t)
{
    size_t i;
    for (i = 0; toupper(s[i]) == toupper(t[i]); i++) {
        if (s[i] == '\0')
            return 0;
    }
    return toupper(s[i]) - toupper(t[i]);
}
