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

#include <stdio.h>
#include <getopt.h>
#include "dvd-audio.h"

#define PTS_PER_SECOND 90000

void
display_options(const char *progname, FILE *output);

int
main(int argc, char *argv[])
{
    char* progname = argv[0];
    char* audio_ts = NULL;

    static struct option long_options[] = {
        {"audio_ts", required_argument, 0, 'A'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    DVDA* dvda;
    DVDA_Titleset* titleset;
    const unsigned titleset_num = 1;
    unsigned title_num;

    /*parse command-line option*/
    do {
        c = getopt_long(argc, argv, "A:h", long_options, &option_index);
        switch (c) {
        case 'h':
            display_options(progname, stdout);
            return 0;
        case 'A':
            audio_ts = optarg;
            break;
        case '?':
            return 1;
        case 0:
        case -1:
        default:
            break;
        }
    } while (c != -1);

    if (!audio_ts) {
        display_options(progname, stdout);
        return 0;
    }

    /*display formatted output*/
    if ((dvda = dvda_open(audio_ts, NULL)) == NULL) {
        fprintf(stderr,
                "*** Error: \"%s\""
                " does not appear to be a valid AUDIO_TS path\n",
                audio_ts);
        return 1;
    }

    if ((titleset = dvda_open_titleset(dvda, titleset_num)) == NULL) {
        fprintf(stderr,
                "*** Error: \"%s\""
                " does not appear to be a valid AUDIO_TS path\n",
                audio_ts);
        dvda_close(dvda);
        return 0;
    }

    printf("Title  Track  Length  "
           "PTS Length  First Sector  Last Sector\n");

    for (title_num = 1;
         title_num <= dvda_title_count(titleset);
         title_num++) {
        DVDA_Title* title = dvda_open_title(titleset, title_num);
        unsigned track_num;

        if (!title) {
            continue;
        }

        for (track_num = 1;
             track_num <= dvda_track_count(title);
             track_num++) {
            DVDA_Track* track = dvda_open_track(title, track_num);
            unsigned pts_length;

            if (!track) {
                continue;
            }

            pts_length = dvda_track_pts_length(track);

            printf("%5u  %5u  %3.1u:%2.2u  %10u  %12u  %11u\n",
                   title_num,
                   track_num,
                   pts_length / PTS_PER_SECOND / 60,
                   pts_length / PTS_PER_SECOND % 60,
                   pts_length,
                   dvda_track_first_sector(track),
                   dvda_track_last_sector(track));

            dvda_close_track(track);
        }

        dvda_close_title(title);
        printf("\n");
    }

    dvda_close_titleset(titleset);

    /*perform cleanup*/
    dvda_close(dvda);

    return 0;
}

void
display_options(const char *progname, FILE *output)
{
    fprintf(output, "*** Usage : %s -A [AUDIO_TS]\n", progname);
    fprintf(output, "Options:\n");
    fprintf(output, "  -h, --help                "
            "show this help message and exit\n");
    fprintf(output, "  -A PATH, --audio_ts=PATH  "
            "path to disc's AUDIO_TS directory\n");
}
