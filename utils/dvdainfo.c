#include "dvda.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    DVDA_t *dvda = dvda_open(argv[1], NULL);

    if (!dvda) {
        printf("missing or invalid AUDIO_TS.IFO file in AUDIO_TS directory\n");
        return 1;
    }

    printf("DVD-A opened successfully\n");
    printf("has %u title sets\n", dvda_titleset_count(dvda));

    dvda_close(dvda);

    return 0;
}
