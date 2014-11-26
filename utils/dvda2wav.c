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
#include <stdlib.h>
#include <string.h>
#include "dvd-audio.h"
#include "bitstream.h"

#define BUFFER_SIZE 4096

void
display_options(const char *progname, FILE *output);

char*
join_paths(const char *path1, const char *path2);

void
extract_tracks(DVDA* dvda, DVDA_Title* title,
               const char *output_dir);

void
extract_track(DVDA* dvda, DVDA_Title* title,
              unsigned track_num, const char *output_dir);

void
extract_track_data(DVDA_Track_Reader* track_reader, const char *output_path);

void
write_wave_header(BitstreamWriter* output,
                  unsigned sample_rate,
                  unsigned channel_count,
                  unsigned channel_mask,
                  unsigned bits_per_sample,
                  unsigned total_pcm_frames);

int
main(int argc, char *argv[])
{
    char* progname = argv[0];
    char* audio_ts = NULL;
    char* cdrom = NULL;
    char* output_dir = ".";
    unsigned title_num = 0;
    unsigned track_num = 0;

    /*parse arguments*/
    static struct option long_options[] = {
        {"audio_ts", required_argument, 0, 'A'},
        {"cdrom", required_argument, 0, 'c'},
        {"title", required_argument, 0, 'T'},
        {"track", required_argument, 0, 't'},
        {"dir", required_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;

    DVDA* dvda;
    DVDA_Titleset* titleset;
    const unsigned titleset_num = 1;

    do {
        c = getopt_long(argc, argv, "A:c:T:t:d:h", long_options, &option_index);

        switch (c) {
        case 'h':
            display_options(progname, stdout);
            return 0;
        case 'v':
            printf("libDVD-Audio %s\n", LIBDVDAUDIO_VERSION_STRING);
            return 0;
        case 'A':
            audio_ts = optarg;
            break;
        case 'c':
            cdrom = optarg;
            break;
        case 'T':
            title_num = (unsigned)strtoul(optarg, NULL, 10);
            break;
        case 't':
            track_num = (unsigned)strtoul(optarg, NULL, 10);
            break;
        case 'd':
            output_dir = optarg;
            break;
        case '?':
            return 1;
        case 0:
        case -1:
            break;
        }
    } while (c != -1);

    if (!audio_ts) {
        display_options(progname, stdout);
        return 0;
    }

    /*open DVD-A*/
    if ((dvda = dvda_open(audio_ts, cdrom)) == NULL) {
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

    if (title_num == 0) {
        /*if no title indicated, extract them all*/
        for (title_num = 1;
             title_num <= dvda_title_count(titleset);
             title_num++) {
             DVDA_Title* title = dvda_open_title(titleset, title_num);
             if (!title) {
                fprintf(stderr, "*** Error: unable to open title %u\n",
                        title_num);
                goto error;
             }
             if (track_num == 0) {
                 extract_tracks(dvda, title, output_dir);
             } else {
                 extract_track(dvda, title, track_num, output_dir);
             }
             dvda_close_title(title);
        }
    } else {
        DVDA_Title* title = dvda_open_title(titleset, title_num);
        if (!title) {
            fprintf(stderr, "*** Error: unable to open title %u\n",
                    title_num);
            goto error;
        }
        if (track_num == 0) {
            extract_tracks(dvda, title, output_dir);
        } else {
            extract_track(dvda, title, track_num, output_dir);
        }
        dvda_close_title(title);
    }

    /*perform cleanup*/
    dvda_close_titleset(titleset);
    dvda_close(dvda);

    return 0;

error:
    dvda_close_titleset(titleset);
    dvda_close(dvda);
    return 1;
}

void
display_options(const char *progname, FILE *output)
{
    fprintf(output, "*** Usage : %s -A [AUDIO_TS] [OPTIONS]\n", progname);
    fprintf(output, "Options:\n");
    fprintf(output, "  -h, --help                "
            "show this help message and exit\n");
    fprintf(output, "  --version                 "
            "display version number and exit\n");
    fprintf(output, "  -A PATH, --audio_ts=PATH  "
            "path to disc's AUDIO_TS directory\n");
    fprintf(output, "  -c DEVICE, --cdrom=DEVICE "
            "optional path to disc's cdrom device\n");
    fprintf(output, "  -T TITLE, --title=TITLE   "
            "title number to extract\n"
                    "                            "
            "if omitted, all titles will be extracted\n");
    fprintf(output, "  -t TRACK, --track=TRACK   "
            "track number to extract\n"
                    "                            "
            "if omitted, all tracks will be extracted\n");
    fprintf(output, "  -d DIR, --dir=DIR         "
            "output directory to place extracted file\n"
                    "                            "
            "if omitted, the current working directory is used\n");
}

static inline int
ends_with(const char *path, char item)
{
    const size_t len = strlen(path);
    if (len) {
        return path[len - 1] == item;
    } else {
        return 0;
    }
}

char*
join_paths(const char *path1, const char *path2)
{
    if (ends_with(path1, '/')) {
        const size_t total_size = strlen(path1) + strlen(path2) + 1;
        char *joined = malloc(total_size);
        snprintf(joined, total_size, "%s%s", path1, path2);
        return joined;
    } else {
        const size_t total_size = strlen(path1) + 1 + strlen(path2) + 1;
        char *joined = malloc(total_size);
        snprintf(joined, total_size, "%s/%s", path1, path2);
        return joined;
    }
}

void
extract_tracks(DVDA* dvda, DVDA_Title* title,
               const char *output_dir)
{
    unsigned track_num;
    for (track_num = 1; track_num <= dvda_track_count(title); track_num++) {
        extract_track(dvda, title, track_num, output_dir);
    }
}

void
extract_track(DVDA* dvda, DVDA_Title* title,
              unsigned track_num, const char *output_dir)
{
    DVDA_Track* track = dvda_open_track(title, track_num);
    DVDA_Track_Reader* track_reader;
    char track_name[] = "track-XX-XX.wav";
    char *output_path;

    if (!track) {
        fprintf(stderr, "*** Error: unable to open track %u\n", track_num);
        return;
    }

    if ((track_reader = dvda_open_track_reader(track)) == NULL) {
        fprintf(stderr, "*** Error: unable to open track %u for reading\n",
                track_num);
        dvda_close_track(track);
        return;
    }

    snprintf(track_name, sizeof(track_name),
             "track-%2.2d-%2.2d.wav",
             dvda_title_number(title),
             dvda_track_number(track));

    output_path = join_paths(output_dir, track_name);
    dvda_close_track(track);

    extract_track_data(track_reader, output_path);

    free(output_path);
    dvda_close_track_reader(track_reader);
}

void
extract_track_data(DVDA_Track_Reader* track_reader, const char *output_path)
{
    FILE *output_file;
    BitstreamWriter *output;
    const unsigned channel_count = dvda_channel_count(track_reader);
    const unsigned bits_per_sample = dvda_bits_per_sample(track_reader);
    int buffer[BUFFER_SIZE * channel_count];
    unsigned frames_read;
    bw_pos_t *file_start;
    unsigned total_pcm_frames = 0;
    bw_write_signed_f write_signed;

    if ((output_file = fopen(output_path, "wb")) == NULL) {
        fprintf(stderr, "*** Error: unable to open \"%s\" for writing\n",
                output_path);
        return;
    }

    printf("Extracting %s track  %u channels  %u Hz  %u bps\n",
           (dvda_codec(track_reader) == DVDA_MLP ? "MLP" : "PCM"),
           dvda_channel_count(track_reader),
           dvda_sample_rate(track_reader),
           dvda_bits_per_sample(track_reader));

    output = bw_open(output_file, BS_LITTLE_ENDIAN);
    write_signed = output->write_signed;

    /*write initial RIFF WAVE header*/
    file_start = output->getpos(output);
    write_wave_header(
        output,
        dvda_sample_rate(track_reader),
        channel_count,
        dvda_riff_wave_channel_mask(track_reader),
        bits_per_sample,
        total_pcm_frames);

    /*transfer data from track reader to data chunk*/
    while ((frames_read = dvda_read(track_reader,
                                    BUFFER_SIZE,
                                    buffer)) > 0) {
        unsigned i;
        for (i = 0; i < frames_read * channel_count; i++) {
            write_signed(output, bits_per_sample, buffer[i]);
        }
        total_pcm_frames += frames_read;
    }

    /*go back and write finished RIFF WAVE header*/
    output->setpos(output, file_start);
    file_start->del(file_start);
    write_wave_header(
        output,
        dvda_sample_rate(track_reader),
        channel_count,
        dvda_riff_wave_channel_mask(track_reader),
        bits_per_sample,
        total_pcm_frames);

    output->close(output);

    printf("* Wrote: \"%s\"\n", output_path);
}

void
write_wave_header(BitstreamWriter* output,
                  unsigned sample_rate,
                  unsigned channel_count,
                  unsigned channel_mask,
                  unsigned bits_per_sample,
                  unsigned total_pcm_frames)
{
    const char fmt[] = "16u 16u 32u 32u 16u 16u 16u 16u 32u 16b";
    const uint8_t RIFF[] = {82, 73, 70, 70};
    const uint8_t WAVE[] = {87, 65, 86, 69};
    const uint8_t fmt_[] = {102, 109, 116, 32};
    const uint8_t data[] = {100, 97, 116, 97};
    const uint8_t sub_format[] = {1, 0, 0, 0, 0, 0, 16, 0,
                                  128, 0, 0, 170, 0, 56, 155, 113};

    const unsigned bytes_per_sample = bits_per_sample / 8;
    const unsigned avg_bytes_per_second =
        sample_rate * channel_count * bytes_per_sample;
    const unsigned block_align =
        channel_count * bytes_per_sample;
    const unsigned data_size =
        bytes_per_sample * channel_count * total_pcm_frames;

    const unsigned total_size =
        bs_format_byte_size("4b" "32u 4b") +
        bs_format_byte_size(fmt) +
        bs_format_byte_size("4b 32u") +
        data_size +
        (data_size % 2);

    output->build(output, "4b" "32u 4b", RIFF, total_size, WAVE);
    output->build(output, "4b 32u", fmt_, bs_format_byte_size(fmt));
    output->build(output, fmt,
                  0xFFFE, /*compression code*/
                  channel_count,
                  sample_rate,
                  avg_bytes_per_second,
                  block_align,
                  bits_per_sample,
                  22, /*CB size*/
                  bits_per_sample,
                  channel_mask,
                  sub_format);
    output->build(output, "4b 32u", data, data_size);
}
