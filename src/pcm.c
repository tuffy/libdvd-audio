#include "pcm.h"
#include "bitstream.h"
#include <stdlib.h>

#define SECTOR_SIZE 2048

extern int
read_pack_header(BitstreamReader *sector_reader,
                 uint64_t *pts,
                 unsigned *SCR_extension,
                 unsigned *bitrate);

extern int
read_packet_header(BitstreamReader* sector_reader,
                   unsigned *stream_id,
                   unsigned *packet_length);

static int
SL16_char_to_int(unsigned char *s);

static int
SL24_char_to_int(unsigned char *s);

struct PCMDecoder_s {
    unsigned bps;
    int (*converter)(unsigned char *);
    unsigned channels;
    unsigned bytes_per_sample;
    unsigned chunk_size;
};

PCMDecoder*
dvda_open_pcmdecoder(unsigned bits_per_sample, unsigned channel_count)
{
    PCMDecoder* decoder = malloc(sizeof(PCMDecoder));

    if (bits_per_sample == 16) {
        decoder->bps = 0;
        decoder->converter = SL16_char_to_int;
    } else {
        decoder->bps = 1;
        decoder->converter = SL24_char_to_int;
    }

    decoder->channels = channel_count;

    decoder->bytes_per_sample = bits_per_sample / 8;

    decoder->chunk_size = decoder->bytes_per_sample * channel_count * 2;

    return decoder;
}

void
dvda_close_pcmdecoder(PCMDecoder* decoder)
{
    free(decoder);
}

void
dvda_pcmdecoder_decode_params(BitstreamReader *packet_reader,
                              struct stream_parameters* parameters)
{
    unsigned first_audio_frame;
    unsigned crc;

    packet_reader->parse(
        packet_reader,
        "16u 8p 4u 4u 4u 4u 8p 8u 8p 8u",
        &first_audio_frame,
        &(parameters->group_0_bps),
        &(parameters->group_1_bps),
        &(parameters->group_0_rate),
        &(parameters->group_1_rate),
        &(parameters->channel_assignment),
        &crc);
}

//unsigned
//dvda_pcmdecoder_decode_packet(PCMDecoder* decoder,
//                              BitstreamReader* packet_reader,
//                              aa_int* samples)
//{
//    BitstreamQueue* sector_data = decoder->sector_data;
//    BitstreamReader* sector_reader = (BitstreamReader*)sector_data;
//
//    sector_data->push(sector_data, SECTOR_SIZE, sector);
//
//    /*skip over the pack header*/
//    if (!setjmp(*br_try(sector_reader))) {
//        uint64_t pts;
//        unsigned SCR_extension;
//        unsigned bitrate;
//
//        read_pack_header(sector_reader, &pts, &SCR_extension, &bitrate);
//        br_etry(sector_reader);
//    } else {
//        br_etry(sector_reader);
//        return 0;
//    }
//
//    /*walk through all the sector's packets*/
//    while (sector_data->size(sector_data)) {
//        unsigned stream_id = 0;
//        unsigned packet_length = 0;
//        BitstreamReader* packet_reader;
//
//        /*for each packet*/
//        if (!setjmp(*br_try(sector_reader))) {
//            if (read_packet_header(sector_reader, &stream_id, &packet_length)) {
//                /*invalid start code*/
//                br_etry(sector_reader);
//                return 0;
//            } else {
//                packet_reader = sector_reader->substream(sector_reader,
//                                                         packet_length);
//            }
//            br_etry(sector_reader);
//        } else {
//            /*I/O error reading packet header or getting substream*/
//            br_etry(sector_reader);
//            return 0;
//        }
//
//        if (!setjmp(*br_try(packet_reader))) {
//            /*if packet is audio*/
//            if (stream_id == 0xBD) {
//                unsigned pad_1_size;
//                unsigned codec_id;
//                unsigned pad_2_size;
//
//                packet_reader->parse(packet_reader, "16p 8u", &pad_1_size);
//                packet_reader->skip_bytes(packet_reader, pad_1_size);
//                packet_reader->parse(packet_reader, "8u 8p 8p 8u",
//                                     &codec_id, &pad_2_size);
//
//                /*and if the audio packet is PCM*/
//                if (codec_id == 0xA0) {
//                    struct stream_parameters parameters;
//
//                    /*ensure its parameters match our stream*/
//                    dvda_pcmdecoder_decode_params(packet_reader, &parameters);
//                    if (dvda_params_equal(&(decoder->parameters),
//                                          &parameters)) {
//                        /*then push its data onto our PCM queue*/
//                        packet_reader->skip_bytes(packet_reader, pad_2_size - 9);
//                        packet_reader->enqueue(
//                            packet_reader,
//                            packet_length - (3 + pad_1_size + 4 + pad_2_size),
//                            decoder->pcm_data);
//                    }
//                    /*skip non-matching PCM packets*/
//                }
//                /*skip non-PCM packets*/
//            }
//            /*ignore non-audio packets*/
//
//            br_etry(packet_reader);
//            packet_reader->close(packet_reader);
//        } else {
//            /*I/O error reading packet*/
//            br_etry(packet_reader);
//            packet_reader->close(packet_reader);
//        }
//    }
//
//    /*then process as much PCM data from the queue as possible*/
//    return pcmdecoder_decode(decoder, samples);
//}

unsigned
dvda_pcmdecoder_decode_packet(PCMDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples)
{
    const static uint8_t AOB_BYTE_SWAP[2][6][36] = {
        { /*16 bps*/
            { 1,  0,  3,  2},                                 /*1 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6},                 /*2 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10}, /*3 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10,
             13, 12, 15, 14},                                 /*4 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10,
             13, 12, 15, 14, 17, 16, 19, 18},                 /*5 ch*/
            { 1,  0,  3,  2,  5,  4,  7,  6,  9,  8, 11, 10,
             13, 12, 15, 14, 17, 16, 19, 18, 21, 20, 23, 22}  /*6 ch*/
        },
        { /*24 bps*/
            {   2,  1,  5,  4,  0,  3},  /*1 ch*/
            {   2,  1,  5,  4,  8,  7,
               11, 10,  0,  3,  6,  9},  /*2 ch*/
            {   8,  7, 17, 16,  6, 15,
                2,  1,  5,  4, 11, 10,
               14, 13,  0,  3,  9, 12},  /*3 ch*/
            {   8,  7, 11, 10, 20, 19,
               23, 22,  6,  9, 18, 21,
                2,  1,  5,  4, 14, 13,
               17, 16,  0,  3, 12, 15},  /*4 ch*/
            {   8,  7, 11, 10, 14, 13,
               23, 22, 26, 25, 29, 28,
                6,  9, 12, 21, 24, 27,
                2,  1,  5,  4, 17, 16,
               20, 19,  0,  3, 15, 18},  /*5 ch*/
            {   8,  7, 11, 10, 26, 25,
               29, 28,  6,  9, 24, 27,
                2,  1,  5,  4, 14, 13,
               17, 16, 20, 19, 23, 22,
               32, 31, 35, 34,  0,  3,
               12, 15, 18, 21, 30, 33}  /*6 ch*/
        }
    };
    const unsigned bps = decoder->bps;
    int (*converter)(unsigned char *) = decoder->converter;
    const unsigned channels = decoder->channels;
    const unsigned bytes_per_sample = decoder->bytes_per_sample;
    const unsigned chunk_size = decoder->chunk_size;
    unsigned processed_frames = 0;

    while (packet_reader->size(packet_reader) >= chunk_size) {
        uint8_t unswapped[36];
        uint8_t* unswapped_ptr = unswapped;
        unsigned i;

        /*swap read bytes to proper order*/
        for (i = 0; i < chunk_size; i++) {
            unswapped[AOB_BYTE_SWAP[bps][channels - 1][i]] =
                (uint8_t)(packet_reader->read(packet_reader, 8));
        }

        /*decode bytes to PCM ints and place them in proper channels*/
        for (i = 0; i < (channels * 2); i++) {
            a_int* channel = samples->_[i % channels];
            channel->append(channel, converter(unswapped_ptr));
            unswapped_ptr += bytes_per_sample;
        }

        processed_frames += 2;
    }

    return processed_frames;
}

static int
SL16_char_to_int(unsigned char *s)
{
    if (s[1] & 0x80) {
        /*negative*/
        return -(int)(0x10000 - ((s[1] << 8) | s[0]));
    } else {
        /*positive*/
        return (int)(s[1] << 8) | s[0];
    }
}

static int
SL24_char_to_int(unsigned char *s)
{
    if (s[2] & 0x80) {
        /*negative*/
        return -(int)(0x1000000 - ((s[2] << 16) | (s[1] << 8) | s[0]));
    } else {
        /*positive*/
        return (int)((s[2] << 16) | (s[1] << 8) | s[0]);
    }
}
