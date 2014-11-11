#include "dvda.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    DVDA_t *dvda = dvda_open(argv[1], NULL);
    DVDA_Titleset_t *titleset;
    const unsigned titleset_num = 1;
    unsigned title_num;

    if (!dvda) {
        printf("missing or invalid AUDIO_TS.IFO file in AUDIO_TS directory\n");
        return 1;
    }

    printf("DVD-A opened successfully\n");
    printf("has %u title sets\n", dvda_titleset_count(dvda));

    titleset = dvda_open_titleset(dvda, titleset_num);

    dvda_close(dvda);

    if (!titleset) {
        printf("missing or invalid titleset %u\n", titleset_num);
        return 1;
    }

    printf("got titleset %u ok\n", titleset_num);
    printf("titleset as %u titles\n", dvda_title_count(titleset));

    for (title_num = 1; title_num <= dvda_title_count(titleset); title_num++) {
        unsigned track_num;
        DVDA_Title_t *title = dvda_open_title(titleset, title_num);

        if (!title) {
            printf("missing or invalid title %u\n", title_num);
            dvda_close_titleset(titleset);
            return 1;
        }

        printf("got title %u ok\n", title_num);
        printf("title PTS length : %u\n", dvda_title_pts_length(title));
        printf("title has %u tracks\n", dvda_track_count(title));

        for (track_num = 1; track_num <= dvda_track_count(title); track_num++) {
            DVDA_Track_t* track = dvda_open_track(title, track_num);

            if (!track) {
                printf("missing or invalid track %u\n", track_num);
                dvda_close_title(title);
                dvda_close_titleset(titleset);
                return 1;
            }

            printf("got track %u ok\n",
                   track_num);
            printf("PTS index    : %u\n",
                   dvda_track_pts_index(track));
            printf("PTS length   : %u\n",
                   dvda_track_pts_length(track));
            printf("first sector : 0x%x\n",
                   dvda_track_first_sector(title, track));
            printf("last sector  : 0x%x\n",
                   dvda_track_last_sector(title, track));

            dvda_close_track(track);
        }

        dvda_close_title(title);
    }

    dvda_close_titleset(titleset);

    return 0;
}
