#include "mlp.h"

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

static unsigned
mlpdecoder_decode(MLPDecoder* decoder, aa_int* samples);

/*returns a single MLP frame from the stream of MLP data
  or NULL of no more MLP frames can be retrieved*/
static BitstreamReader*
read_mlp_frame(BitstreamReader* mlp_data);

/*decodes a single MLP frame to a set of samples
  and returns the number of PCM frames decoded*/
static unsigned
decode_mlp_frame(MLPDecoder* decoder,
                 BitstreamReader* mlp_frame,
                 aa_int* samples);


struct MLPDecoder_s {
    struct stream_parameters parameters;
    BitstreamQueue* sector_data;
    BitstreamQueue* mlp_data;
};

MLPDecoder*
dvda_open_mlpdecoder(const struct stream_parameters* parameters)
{
    MLPDecoder* decoder = malloc(sizeof(MLPDecoder));

    decoder->parameters = *parameters;
    decoder->sector_data = br_open_queue(BS_BIG_ENDIAN);
    decoder->mlp_data = br_open_queue(BS_BIG_ENDIAN);

    return decoder;
}

void
dvda_close_mlpdecoder(MLPDecoder* decoder)
{
    decoder->sector_data->close(decoder->sector_data);
    decoder->mlp_data->close(decoder->mlp_data);
    free(decoder);
}

unsigned
dvda_mlpdecoder_decode_sector(MLPDecoder* decoder,
                              const uint8_t sector[],
                              aa_int* samples)
{
    BitstreamQueue* sector_data = decoder->sector_data;
    BitstreamReader* sector_reader = (BitstreamReader*)sector_data;

    sector_data->push(sector_data, SECTOR_SIZE, sector);

    /*skip over the pack header*/
    if (!setjmp(*br_try(sector_reader))) {
        uint64_t pts;
        unsigned SCR_extension;
        unsigned bitrate;

        read_pack_header(sector_reader, &pts, &SCR_extension, &bitrate);
        br_etry(sector_reader);
    } else {
        br_etry(sector_reader);
        return 0;
    }

    /*walk through all the sector's packets*/
    while (sector_data->size(sector_data)) {
        unsigned stream_id = 0;
        unsigned packet_length = 0;
        BitstreamReader* packet_reader;

        /*for each packet*/
        if (!setjmp(*br_try(sector_reader))) {
            if (read_packet_header(sector_reader, &stream_id, &packet_length)) {
                /*invalid start code*/
                br_etry(sector_reader);
                return 0;
            } else {
                packet_reader = sector_reader->substream(sector_reader,
                                                         packet_length);
            }
            br_etry(sector_reader);
        } else {
            /*I/O error reading packet header or getting substream*/
            br_etry(sector_reader);
            return 0;
        }

        if (!setjmp(*br_try(packet_reader))) {
            /*if the packet is audio*/
            if (stream_id == 0xBD) {
                unsigned pad_1_size;
                unsigned codec_id;
                unsigned pad_2_size;

                packet_reader->parse(packet_reader, "16p 8u", &pad_1_size);
                packet_reader->skip_bytes(packet_reader, pad_1_size);
                packet_reader->parse(packet_reader, "8u 8p 8p 8u",
                                     &codec_id, &pad_2_size);

                /*and if the audio packet is MLP*/
                if (codec_id == 0xA1) {
                    /*then push its data onto our MLP queue*/
                    packet_reader->skip_bytes(packet_reader, pad_2_size);
                    packet_reader->enqueue(
                        packet_reader,
                        packet_length - (3 + pad_1_size + 4 + pad_2_size),
                        decoder->mlp_data);
                }
                /*skip non-MLP packets*/
            }
            /*ignore non-audio packets*/

            br_etry(packet_reader);
            packet_reader->close(packet_reader);
        } else {
            /*I/O error reading packet*/
            br_etry(packet_reader);
            packet_reader->close(packet_reader);
            fprintf(stderr, "I/O error reading packet\n");
        }
    }

    /*then process as much PCM data from the queue as possible*/
    return mlpdecoder_decode(decoder, samples);
}

unsigned
dvda_mlpdecoder_flush(MLPDecoder* decoder,
                      aa_int* samples)
{
    /*FIXME - have this do something*/
    return 0;
}

static unsigned
mlpdecoder_decode(MLPDecoder* decoder, aa_int* samples)
{
    unsigned pcm_frames_decoded = 0;
    BitstreamReader* mlp_frame;

    while ((mlp_frame =
            read_mlp_frame((BitstreamReader*)decoder->mlp_data)) != NULL) {
        if (!setjmp(*br_try(mlp_frame))) {
            pcm_frames_decoded += decode_mlp_frame(decoder,
                                                   mlp_frame,
                                                   samples);
            br_etry(mlp_frame);
            mlp_frame->close(mlp_frame);
        } else {
            /*I/O error decoding MLP frame*/
            fprintf(stderr, "I/O error decoding MLP frame\n");
            br_etry(mlp_frame);
            mlp_frame->close(mlp_frame);
        }
    }

    return pcm_frames_decoded;
}

static BitstreamReader*
read_mlp_frame(BitstreamReader* mlp_data)
{
    br_pos_t* start = mlp_data->getpos(mlp_data);  /*should always succeed*/
    if (!setjmp(*br_try(mlp_data))) {
        unsigned total_frame_size;
        BitstreamReader* mlp_frame;

        mlp_data->parse(mlp_data, "4p 12u 16p", &total_frame_size);
        total_frame_size *= 2;
        fprintf(stderr, "total frame size %u\n", total_frame_size);
        mlp_frame = mlp_data->substream(mlp_data, total_frame_size - 4);
        br_etry(mlp_data);
        start->del(start);
        fprintf(stderr, "got %u byte MLP frame\n", total_frame_size);
        return mlp_frame;
    } else {
        /*some I/O error reading the next MLP frame*/
        br_etry(mlp_data);
        mlp_data->setpos(mlp_data, start);
        start->del(start);
        return NULL;
    }
}

static unsigned
decode_mlp_frame(MLPDecoder* decoder,
                 BitstreamReader* mlp_frame,
                 aa_int* samples)
{
    /*FIXME - have this do something*/
    return 0;
}
