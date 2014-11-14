#include "aob.h"
#include "audio_ts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR_SIZE 2048

struct AOB {
    FILE *file;
    unsigned total_sectors;
    unsigned current_sector;
};

struct AOB_Reader_s {
    struct AOB AOB[10];
    unsigned total_aobs;
    unsigned current_aob;
};

/*******************************************************************
 *                   private function definitions                  *
 *******************************************************************/

/*returns 0 on success, 1 on failure*/
static int
aob_open(const char *aob_path, struct AOB *aob);

static inline void
aob_close(struct AOB *aob)
{
    fclose(aob->file);
    aob->total_sectors = aob->current_sector = 0;
}

/*reads exactly 2048 bytes to the given buffer
  and returns 0 on sucess, 1 on failure*/
static int
aob_read_sector(struct AOB *aob, uint8_t *sector_data);

static inline void
aob_seek_sector(struct AOB *aob, unsigned sector_number)
{
    if (sector_number < aob->total_sectors) {
        aob->current_sector = sector_number;
    } else {
        aob->current_sector = aob->total_sectors;
    }
    fseek(aob->file, aob->current_sector * SECTOR_SIZE, SEEK_SET);
}

static inline struct AOB*
aob_current(AOB_Reader *reader)
{
    return &(reader->AOB[reader->current_aob]);
}

/*******************************************************************
 *                  public function implementations                *
 *******************************************************************/

AOB_Reader*
aob_reader_open(const char *audio_ts_path,
                const char *cdrom_device,
                unsigned titleset)
{
    unsigned aob_number;
    AOB_Reader *reader = malloc(sizeof(AOB_Reader));
    reader->total_aobs = 0;
    reader->current_aob = 0;

    for (aob_number = 1; aob_number <= 9; aob_number++) {
        char aob_name[] = "ATS_XX_X.AOB";
        char *aob_path;
        snprintf(aob_name,
                 strlen(aob_name) + 1,
                 "ATS_%2.2d_%1.1d.AOB",
                 titleset,
                 aob_number);
        aob_path = find_audio_ts_file(audio_ts_path, aob_name);
        if (aob_path) {
            int open_ok = !aob_open(aob_path,
                                    &(reader->AOB[reader->total_aobs]));

            free(aob_path);
            if (open_ok) {
                reader->total_aobs += 1;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return reader;
}

void
aob_reader_close(AOB_Reader *reader)
{
    unsigned i;
    for (i = 0; i < reader->total_aobs; i++) {
        aob_close(&reader->AOB[i]);
    }
    free(reader);
}

int
aob_reader_read(AOB_Reader *reader, uint8_t *sector_data)
{
    if (aob_read_sector(aob_current(reader), sector_data)) {
        /*error reading sector in current AOB file, so try next AOB file*/
        reader->current_aob++;
        if (reader->current_aob < reader->total_aobs) {
            return aob_reader_read(reader, sector_data);
        } else {
            /*no more data to read*/
            return 1;
        }
    } else {
        /*sector read OK*/
        return 0;
    }
}

int
aob_reader_seek(AOB_Reader *reader, unsigned sector_number)
{
    unsigned current_aob;

    for (current_aob = 0; current_aob < reader->total_aobs; current_aob++) {
        const unsigned aob_sectors =
            reader->AOB[current_aob].total_sectors;
        if (sector_number < aob_sectors) {
            reader->current_aob = current_aob;
            aob_seek_sector(aob_current(reader), sector_number);
            return 0;
        } else {
            sector_number -= aob_sectors;
        }
    }
    /*ran out of AOBs before sector is found*/
    return 1;
}

/*******************************************************************
 *                  private function implementations               *
 *******************************************************************/
static int
aob_open(const char *aob_path, struct AOB *aob)
{
    struct stat aob_stat;

    if (stat(aob_path, &aob_stat)) {
        return 1;
    }
    if ((aob->file = fopen(aob_path, "rb")) == NULL) {
        return 1;
    }
    aob->total_sectors = aob_stat.st_size / SECTOR_SIZE;
    aob->current_sector = 0;
    return 0;
}

static int
aob_read_sector(struct AOB *aob, uint8_t *sector_data)
{
    if (aob->current_sector < aob->total_sectors) {
        if (fread(sector_data, sizeof(uint8_t), SECTOR_SIZE, aob->file) ==
                SECTOR_SIZE) {
            /*sector read okay*/
            aob->current_sector += 1;
            return 0;
        } else {
            /*read error*/
            return 1;
        }
    } else {
        /*no more sectors to read*/
        return 1;
    }
}