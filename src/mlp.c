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

#include "mlp.h"

#define SECTOR_SIZE 2048

/*streams can have only 1 or 2 substreams*/
#define MAX_MLP_SUBSTREAMS 2

#define MAX_MLP_MATRICES 6

/*6 channels + 2 matrix channels*/
#define MAX_MLP_CHANNELS 8

/*******************************************************************
 *                      structure definitions                      *
 *******************************************************************/

struct major_sync {
    unsigned sync_words;
    unsigned stream_type;
    struct stream_parameters parameters;
    unsigned is_VBR;
    unsigned peak_bitrate;
    unsigned substream_count;
};

struct substream_info {
    unsigned extraword_present;
    unsigned nonrestart_substream;
    unsigned checkdata_present;
    unsigned substream_end;
};

struct restart_header {
    unsigned min_channel;
    unsigned max_channel;
    unsigned max_matrix_channel;
    unsigned noise_shift;
    unsigned noise_gen_seed;
    unsigned channel_assignment[MAX_MLP_CHANNELS];
    unsigned checksum;
};

struct matrix_parameters {
    unsigned out_channel;
    unsigned factional_bits;
    unsigned LSB_bypass;
    int coeff[MAX_MLP_CHANNELS];
    a_int* bypassed_LSB;
};

struct filter_parameters {
    unsigned shift;
    a_int* coeff;
    a_int* state;
};

struct channel_parameters {
    struct filter_parameters FIR;
    struct filter_parameters IIR;

    int huffman_offset;
    unsigned codebook;
    unsigned huffman_lsbs;
};

struct decoding_parameters {
    unsigned flags[8];

    unsigned block_size;

    unsigned matrix_len;
    struct matrix_parameters matrix[MAX_MLP_MATRICES];

    unsigned output_shift[MAX_MLP_CHANNELS];

    unsigned quant_step_size[MAX_MLP_CHANNELS];

    struct channel_parameters channel[MAX_MLP_CHANNELS];
};

struct substream {
    struct substream_info info;

    struct restart_header header;

    struct decoding_parameters parameters;

    /*residuals[c][i] where c is channel and i is PCM frame*/
    aa_int* residuals;

    /*a temporary buffer of filtered residual data*/
    a_int* filtered;
};

struct MLPDecoder_s {
    struct stream_parameters parameters;
    BitstreamQueue* mlp_data;

    struct major_sync major_sync;
    int major_sync_read;

    struct substream substream[MAX_MLP_SUBSTREAMS];

    aa_int* framelist;
};

struct checkdata {
    uint8_t parity;
    uint8_t crc;
    uint8_t final_crc;
};

/*******************************************************************
 *                   private function definitions                  *
 *******************************************************************/

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

/*attemps to read a major sync from the MLP frame
  returns 1 if successful
  returns 0 if unsuccessful and leaves the stream unchanged*/
static int
read_major_sync(BitstreamReader *mlp_frame, struct major_sync *major_sync);

/*attempts to read a substream info block from the MLP frame
  returns 1 if successful, 0 if unsuccessful*/
static int
read_substream_info(BitstreamReader *mlp_frame,
                    struct substream_info *substream_info);

/*returns a frame's substream data
  or NULL if a parity or CRC-8 mismatch occurs*/
static BitstreamReader*
read_substream(BitstreamReader *mlp_frame,
               unsigned substream_length,
               unsigned checkdata_present);

static unsigned
decode_substream(struct substream* substream,
                 BitstreamReader* substream_reader,
                 aa_int* framelist);

static unsigned
decode_block(struct substream* substream,
             BitstreamReader* substream_reader,
             aa_int* framelist);

static int
decode_restart_header(BitstreamReader* substream_reader,
                      struct restart_header* restart_header);

static int
decode_decoding_parameters(BitstreamReader* substream_reader,
                           unsigned header_present,
                           unsigned min_channel,
                           unsigned max_channel,
                           unsigned max_matrix_channel,
                           struct decoding_parameters* p);

static int
decode_matrix_parameters(BitstreamReader* substream_reader,
                         unsigned max_matrix_channel,
                         unsigned* matrix_len,
                         struct matrix_parameters* mp);

static int
decode_FIR_parameters(BitstreamReader* substream_reader,
                      struct filter_parameters* FIR);

static int
decode_IIR_parameters(BitstreamReader* substream_reader,
                      struct filter_parameters* IIR);

static int
decode_residual_data(BitstreamReader* substream_reader,
                     unsigned min_channel,
                     unsigned max_channel,
                     unsigned block_size,
                     unsigned matrix_len,
                     const struct matrix_parameters* matrix,
                     const unsigned* quant_step_size,
                     const struct channel_parameters* channel,
                     aa_int* residuals);

static int
filter_channel(const a_int* residuals,
               struct filter_parameters* FIR,
               struct filter_parameters* IIR,
               unsigned quant_step_size,
               a_int* filtered);

/*given a list of filtered residuals across all substreams
  max_matrix_channel, noise_shift, noise_gen_seed from the restart header
  matrix parameters, quant_step_size from the decoding parameters
  and bypassed_LSBs from the residual block
  returns a set of rematrixed channel data

  when 2 substreams are present in an MLP stream,
  one typically uses the parameters from the second substream*/
static void
rematrix_channels(aa_int* channels,
                  unsigned max_matrix_channel,
                  unsigned noise_shift,
                  unsigned* noise_gen_seed,
                  unsigned matrix_count,
                  const struct matrix_parameters* matrix,
                  const unsigned* quant_step_size);

static void
checkdata_callback(uint8_t byte, struct checkdata *checkdata);

static inline int
mask(int x, unsigned q)
{
    if (q == 0)
        return x;
    else
        return (x >> q) << q;
}

static inline unsigned
flag_set(BitstreamReader* reader)
{
    return reader->read(reader, 1);
}

/*******************************************************************
 *                  public function implementations                *
 *******************************************************************/

MLPDecoder*
dvda_open_mlpdecoder(const struct stream_parameters* parameters)
{
    unsigned c;
    unsigned s;

    MLPDecoder* decoder = malloc(sizeof(MLPDecoder));

    decoder->parameters = *parameters;
    decoder->mlp_data = br_open_queue(BS_BIG_ENDIAN);

    decoder->major_sync_read = 0;

    /*initialize placeholder framelist*/
    decoder->framelist = aa_int_new();
    for (c = 0; c < MAX_MLP_CHANNELS; c++)
        (void)decoder->framelist->append(decoder->framelist);

    for (s = 0; s < MAX_MLP_SUBSTREAMS; s++) {
        unsigned m;

        decoder->substream[s].residuals = aa_int_new();
        decoder->substream[s].filtered = a_int_new();

        /*init matrix parameters*/
        for (m = 0; m < MAX_MLP_MATRICES; m++) {
            decoder->substream[s].parameters.matrix[m].bypassed_LSB =
                a_int_new();
        }

        /*init channel parameters*/
        for (c = 0; c < MAX_MLP_CHANNELS; c++) {
            decoder->substream[s].parameters.channel[c].FIR.coeff =
                a_int_new();
            decoder->substream[s].parameters.channel[c].FIR.state =
                a_int_new();
            decoder->substream[s].parameters.channel[c].IIR.coeff =
                a_int_new();
            decoder->substream[s].parameters.channel[c].IIR.state =
                a_int_new();
        }
    }

    return decoder;
}

void
dvda_close_mlpdecoder(MLPDecoder* decoder)
{
    unsigned s;

    decoder->mlp_data->close(decoder->mlp_data);

    decoder->framelist->del(decoder->framelist);

    for (s = 0; s < MAX_MLP_SUBSTREAMS; s++) {
        unsigned c;
        unsigned m;

        aa_int_del(decoder->substream[s].residuals);
        a_int_del(decoder->substream[s].filtered);

        /*free matrix parameters*/
        for (m = 0; m < MAX_MLP_MATRICES; m++) {
            a_int_del(
                decoder->substream[s].parameters.matrix[m].bypassed_LSB);
        }

        /*free channel parameters*/
        for (c = 0; c < MAX_MLP_CHANNELS; c++) {
            a_int_del(decoder->substream[s].parameters.channel[c].FIR.coeff);
            a_int_del(decoder->substream[s].parameters.channel[c].FIR.state);
            a_int_del(decoder->substream[s].parameters.channel[c].IIR.coeff);
            a_int_del(decoder->substream[s].parameters.channel[c].IIR.state);
        }
    }

    free(decoder);
}

unsigned
dvda_mlpdecoder_decode_packet(MLPDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples)
{
    packet_reader->enqueue(packet_reader,
                           packet_reader->size(packet_reader),
                           decoder->mlp_data);

    return mlpdecoder_decode(decoder, samples);
}

/*******************************************************************
 *                  private function implementations               *
 *******************************************************************/

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
        mlp_frame = mlp_data->substream(mlp_data, total_frame_size - 4);
        br_etry(mlp_data);
        start->del(start);
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

    /*WAVE_CHANEL[a][c] where a is 5 bit channel assignment field
      and c is the MLP channel index
      yields the RIFF WAVE channel index*/
    const static int WAVE_CHANNEL[][6] = {
        /* 0x00 */ {  0, -1, -1, -1, -1, -1},
        /* 0x01 */ {  0,  1, -1, -1, -1, -1},
        /* 0x02 */ {  0,  1,  2, -1, -1, -1},
        /* 0x03 */ {  0,  1,  2,  3, -1, -1},
        /* 0x04 */ {  0,  1,  2, -1, -1, -1},
        /* 0x05 */ {  0,  1,  2,  3, -1, -1},
        /* 0x06 */ {  0,  1,  2,  3,  4, -1},
        /* 0x07 */ {  0,  1,  2, -1, -1, -1},
        /* 0x08 */ {  0,  1,  2,  3, -1, -1},
        /* 0x09 */ {  0,  1,  2,  3,  4, -1},
        /* 0x0A */ {  0,  1,  2,  3, -1, -1},
        /* 0x0B */ {  0,  1,  2,  3,  4, -1},
        /* 0x0C */ {  0,  1,  2,  3,  4,  5},
        /* 0x0D */ {  0,  1,  2,  3, -1, -1},
        /* 0x0E */ {  0,  1,  2,  3,  4, -1},
        /* 0x0F */ {  0,  1,  2,  3, -1, -1},
        /* 0x10 */ {  0,  1,  2,  3,  4, -1},
        /* 0x11 */ {  0,  1,  2,  3,  4,  5},
        /* 0x12 */ {  0,  1,  3,  4,  2, -1},
        /* 0x13 */ {  0,  1,  3,  4,  2, -1},
        /* 0x14 */ {  0,  1,  4,  5,  2,  3}
    };
    struct major_sync major_sync;
    struct substream* substream0 = &(decoder->substream[0]);
    struct substream* substream1 = &(decoder->substream[1]);
    unsigned s;
    unsigned m;
    unsigned c;
    unsigned pcm_frames[2];
    BitstreamReader *substream_reader;

    /*FIXME - handle channel reordering here*/

    /*check for major sync*/
    if (read_major_sync(mlp_frame, &major_sync)) {
        if (decoder->major_sync_read) {
            /*ensure new major sync matches previously read one*/
            if (!dvda_params_equal(&(decoder->major_sync.parameters),
                                   &(major_sync.parameters))) {
                return 0;
            }
        } else {
            decoder->major_sync = major_sync;
            decoder->major_sync_read = 1;
        }
    }

    /*read 1 substream info per substream*/
    for (s = 0; s < decoder->major_sync.substream_count; s++) {
        if (!read_substream_info(mlp_frame, &(decoder->substream[s].info))) {
            /*invalid extraword present value*/
            return 0;
        }
    }

    /*read substream 0, which will always be present*/
    if ((substream_reader =
         read_substream(mlp_frame,
                        substream0->info.substream_end,
                        substream0->info.checkdata_present)) == NULL) {
        /*error in substream's parity or CRC-8*/
        assert(0);
        return 0;
    }

    /*clear the bypassed LSB values in substream 0's matrix data*/
    for (m = 0; m < MAX_MLP_MATRICES; m++)
        a_int_reset(substream0->parameters.matrix[m].bypassed_LSB);

    /*decode substream 0 bytes to channel data*/
    if (!setjmp(*br_try(substream_reader))) {
        pcm_frames[0] = decode_substream(substream0,
                                         substream_reader,
                                         decoder->framelist);
        br_etry(substream_reader);
        substream_reader->close(substream_reader);
    } else {
        /*I/O error decoding substream*/
        br_etry(substream_reader);
        substream_reader->close(substream_reader);
        assert(0);
        return 0;
    }

    if (!pcm_frames[0]) {
        assert(0);
        return 0;
    }

    if (decoder->major_sync.substream_count == 1) {
        /*rematrix substream 0*/
        rematrix_channels(decoder->framelist,
                          substream0->header.max_matrix_channel,
                          substream0->header.noise_shift,
                          &(substream0->header.noise_gen_seed),
                          substream0->parameters.matrix_len,
                          substream0->parameters.matrix,
                          substream0->parameters.quant_step_size);

        /*apply output shifts to substream 0*/
        for (c = 0; c <= substream0->header.max_matrix_channel; c++) {
            const unsigned output_shift =
                substream0->parameters.output_shift[c];
            if (output_shift) {
                const unsigned block_size = decoder->framelist->_[c]->len;
                unsigned i;
                for (i = 0; i < block_size; i++) {
                    decoder->framelist->_[c]->_[i] <<= output_shift;
                }
            }
        }

        /*append rematrixed data to output in RIFF WAVE order*/
        for (c = 0; c < samples->len; c++) {
            a_int* out_channel = samples->_[
                WAVE_CHANNEL[
                    decoder->major_sync.parameters.channel_assignment][c]];
            out_channel->extend(out_channel, decoder->framelist->_[c]);
        }

        /*clear out framelist for next run*/
        for (c = 0; c < decoder->framelist->len; c++) {
            decoder->framelist->_[c]->reset(decoder->framelist->_[c]);
        }
    } else {
        /*read substream 1*/
        if ((substream_reader =
             read_substream(mlp_frame,
                            substream1->info.substream_end -
                            substream0->info.substream_end,
                            substream0->info.checkdata_present)) == NULL) {
            /*error in substream's parity or CRC-8*/
            assert(0);
            return 0;
        }

        /*clear the bypassed LSB values in substream 1's matrix data*/
        for (m = 0; m < MAX_MLP_MATRICES; m++)
            a_int_reset(substream1->parameters.matrix[m].bypassed_LSB);

        /*decode substream 1 bytes to channel data*/
        if (!setjmp(*br_try(substream_reader))) {
            pcm_frames[1] = decode_substream(substream1,
                                             substream_reader,
                                             decoder->framelist);
            br_etry(substream_reader);
            substream_reader->close(substream_reader);
        } else {
            /*I/O error decoding substream*/
            br_etry(substream_reader);
            substream_reader->close(substream_reader);
            assert(0);
            return 0;
        }

        if (!pcm_frames[1]) {
            assert(0);
            return 0;
        }

        /*rematrix substreams 0 and 1*/
        rematrix_channels(decoder->framelist,
                          substream1->header.max_matrix_channel,
                          substream1->header.noise_shift,
                          &(substream1->header.noise_gen_seed),
                          substream1->parameters.matrix_len,
                          substream1->parameters.matrix,
                          substream1->parameters.quant_step_size);

        /*apply output shifts to substreams 0 and 1*/
        for (c = 0; c <= substream1->header.max_matrix_channel; c++) {
            const unsigned output_shift =
                substream1->parameters.output_shift[c];
            if (output_shift) {
                const unsigned block_size = decoder->framelist->_[c]->len;
                unsigned i;
                for (i = 0; i < block_size; i++) {
                    decoder->framelist->_[c]->_[i] <<= output_shift;
                }
            }
        }

        /*append rematrixed data to output in RIFF WAVE order*/
        for (c = 0; c < samples->len; c++) {
            a_int* out_channel = samples->_[
                WAVE_CHANNEL[
                    decoder->major_sync.parameters.channel_assignment][c]];
            out_channel->extend(out_channel, decoder->framelist->_[c]);
        }

        /*clear out framelist for next run*/
        for (c = 0; c < decoder->framelist->len; c++) {
            decoder->framelist->_[c]->reset(decoder->framelist->_[c]);
        }
    }

    return pcm_frames[0];
}

static int
read_major_sync(BitstreamReader *mlp_frame, struct major_sync *major_sync)
{
    br_pos_t* frame_start = mlp_frame->getpos(mlp_frame);
    if (!setjmp(*br_try(mlp_frame))) {
        int valid_major_sync;

        mlp_frame->parse(mlp_frame,
                         "24u 8u 4u 4u 4u 4u 11p 5u 48p 1u 15u 4u 92p",
                         &(major_sync->sync_words),
                         &(major_sync->stream_type),
                         &(major_sync->parameters.group_0_bps),
                         &(major_sync->parameters.group_1_bps),
                         &(major_sync->parameters.group_0_rate),
                         &(major_sync->parameters.group_1_rate),
                         &(major_sync->parameters.channel_assignment),
                         &(major_sync->is_VBR),
                         &(major_sync->peak_bitrate),
                         &(major_sync->substream_count));

        br_etry(mlp_frame);

        valid_major_sync = ((major_sync->sync_words == 0xF8726F) &&
                            (major_sync->stream_type == 0xBB) &&
                            ((major_sync->substream_count == 1) ||
                             (major_sync->substream_count == 2)));

        if (!valid_major_sync) {
            mlp_frame->setpos(mlp_frame, frame_start);
        }
        frame_start->del(frame_start);
        return valid_major_sync;
    } else {
        /*some I/O error occurred reading major sync
          this is not necessarily an error*/
        br_etry(mlp_frame);
        mlp_frame->setpos(mlp_frame, frame_start);
        frame_start->del(frame_start);
        return 0;
    }
}

static int
read_substream_info(BitstreamReader *mlp_frame,
                    struct substream_info *substream_info)
{
    mlp_frame->parse(mlp_frame,
                     "1u 1u 1u 1p 12u",
                     &(substream_info->extraword_present),
                     &(substream_info->nonrestart_substream),
                     &(substream_info->checkdata_present),
                     &(substream_info->substream_end));

    substream_info->substream_end *= 2;

    return (substream_info->extraword_present == 0);
}

static BitstreamReader*
read_substream(BitstreamReader *mlp_frame,
               unsigned substream_length,
               unsigned checkdata_present)
{
    if (checkdata_present) {
        /*checkdata present, so last 2 bytes are CRC-8/parity*/
        struct checkdata checkdata = {0, 0x3C, 0};
        uint8_t parity;
        uint8_t CRC8;
        BitstreamReader* substream;

        mlp_frame->add_callback(mlp_frame,
                                (bs_callback_f)checkdata_callback,
                                &checkdata);

        substream = mlp_frame->substream(mlp_frame, substream_length - 2);

        mlp_frame->pop_callback(mlp_frame, NULL);

        parity = (uint8_t)mlp_frame->read(mlp_frame, 8);

        if ((parity ^ checkdata.parity) != 0xA9) {
            substream->close(substream);
            /*parity mismatch*/
            fprintf(stderr, "parity mismatch\n");
            return NULL;
        }

        CRC8 = (uint8_t)mlp_frame->read(mlp_frame, 8);

        if (checkdata.final_crc != CRC8) {
            substream->close(substream);
            /*CRC-8 mismatch*/
            fprintf(stderr, "CRC-8 mismatch\n");
            return NULL;
        }

        return substream;
    } else {
        return mlp_frame->substream(mlp_frame, substream_length);
    }
}

static unsigned
decode_substream(struct substream* substream,
                 BitstreamReader* substream_reader,
                 aa_int* framelist)
{
    unsigned pcm_frames_decoded = 0;

    do {
        const unsigned block_frames =
            decode_block(substream, substream_reader, framelist);
        if (block_frames > 0) {
            pcm_frames_decoded += block_frames;
        } else {
            return pcm_frames_decoded;
        }
    } while (flag_set(substream_reader) == 0);

    return pcm_frames_decoded;
}

static unsigned
decode_block(struct substream* substream,
             BitstreamReader* sr,
             aa_int* framelist)
{
    unsigned c;

    /*decoding parameters present*/
    if (flag_set(sr)) {
        const unsigned restart_header = flag_set(sr);

        /*restart header present*/
        if (restart_header) {
            if (!decode_restart_header(sr, &(substream->header))) {
                /*error reading restart header*/
                assert(0);
                return 0;
            }
        }

        if (!decode_decoding_parameters(sr,
                                        restart_header,
                                        substream->header.min_channel,
                                        substream->header.max_channel,
                                        substream->header.max_matrix_channel,
                                        &(substream->parameters))) {
            /*error reading decoding parameters*/
            assert(0);
            return 0;
        }
    }

    /*perform residuals decoding*/
    if (!decode_residual_data(sr,
                              substream->header.min_channel,
                              substream->header.max_channel,
                              substream->parameters.block_size,
                              substream->parameters.matrix_len,
                              substream->parameters.matrix,
                              substream->parameters.quant_step_size,
                              substream->parameters.channel,
                              substream->residuals)) {
        /*error reading residuals*/
        assert(0);
        return 0;
    }


    /*filter residuals based on FIR/IIR parameters*/
    for (c = substream->header.min_channel;
         c <= substream->header.max_channel;
         c++) {
        if (!filter_channel(substream->residuals->_[c],
                            &(substream->parameters.channel[c].FIR),
                            &(substream->parameters.channel[c].IIR),
                            substream->parameters.quant_step_size[c],
                            substream->filtered)) {
            assert(0);
            return 0;
        }

        /*append filtered data to framelist*/
        framelist->_[c]->extend(framelist->_[c], substream->filtered);
    }

    return substream->parameters.block_size;
}

static int
decode_restart_header(BitstreamReader* sr,
                      struct restart_header* restart_header)
{
    unsigned header_sync;
    unsigned noise_type;
    unsigned output_timestamp;
    unsigned check_data_present;
    unsigned lossless_check;
    unsigned c;
    unsigned unknown1;
    unsigned unknown2;

    sr->parse(sr, "13u 1u 16u 4u 4u 4u 4u 23u 19u 1u 8u 16u",
              &header_sync, &noise_type, &output_timestamp,
              &(restart_header->min_channel),
              &(restart_header->max_channel),
              &(restart_header->max_matrix_channel),
              &(restart_header->noise_shift),
              &(restart_header->noise_gen_seed),
              &unknown1,
              &check_data_present,
              &lossless_check,
              &unknown2);

    if (header_sync != 0x18F5)
        return 0;
    if (noise_type != 0)
        return 0;
    if (restart_header->max_channel < restart_header->min_channel)
        return 0;
    if (restart_header->max_matrix_channel < restart_header->max_channel)
        return 0;

    for (c = 0; c <= restart_header->max_matrix_channel; c++) {
        if ((restart_header->channel_assignment[c] =
             sr->read(sr, 6)) >
            restart_header->max_matrix_channel) {
            return 0;
        }
    }

    restart_header->checksum = sr->read(sr, 8);

    return 1;
}

static int
decode_decoding_parameters(BitstreamReader* sr,
                           unsigned header_present,
                           unsigned min_channel,
                           unsigned max_channel,
                           unsigned max_matrix_channel,
                           struct decoding_parameters* p)
{
    unsigned c;

    /*parameter presence flags*/
    if (header_present) {
        if (flag_set(sr)) {
            sr->parse(sr, "8*1u",
                      &(p->flags[0]),
                      &(p->flags[1]),
                      &(p->flags[2]),
                      &(p->flags[3]),
                      &(p->flags[4]),
                      &(p->flags[5]),
                      &(p->flags[6]),
                      &(p->flags[7]));
        } else {
            p->flags[0] =
            p->flags[1] =
            p->flags[2] =
            p->flags[3] =
            p->flags[4] =
            p->flags[5] =
            p->flags[6] =
            p->flags[7] = 1;
        }
    } else if (p->flags[0] && flag_set(sr)) {
        sr->parse(sr, "8*1u",
                  &(p->flags[0]),
                  &(p->flags[1]),
                  &(p->flags[2]),
                  &(p->flags[3]),
                  &(p->flags[4]),
                  &(p->flags[5]),
                  &(p->flags[6]),
                  &(p->flags[7]));
    }

    /*block size*/
    if (p->flags[7] && flag_set(sr)) {
        if ((p->block_size = sr->read(sr, 9)) < 8)
            return 0;
    } else if (header_present) {
        p->block_size = 8;
    }

    /*matrix parameters*/
    if (p->flags[6] && flag_set(sr)) {
        if (!decode_matrix_parameters(sr,
                                      max_matrix_channel,
                                      &(p->matrix_len),
                                      p->matrix))
            return 0;
    } else if (header_present) {
        p->matrix_len = 0;
    }

    /*output shifts*/
    if (p->flags[5] && flag_set(sr)) {
        for (c = 0; c <= max_matrix_channel; c++)
            p->output_shift[c] =
                sr->read_signed(sr, 4);
    } else if (header_present) {
        for (c = 0; c < MAX_MLP_CHANNELS; c++)
            p->output_shift[c] = 0;
    }

    /*quant step sizes*/
    if (p->flags[4] && flag_set(sr)) {
        for (c = 0; c <= max_channel; c++) {
            p->quant_step_size[c] = sr->read(sr, 4);
        }
    } else if (header_present) {
        for (c = 0; c < MAX_MLP_CHANNELS; c++) {
            p->quant_step_size[c] = 0;
        }
    }

    /*channel parameters*/
    for (c = min_channel; c <= max_channel; c++) {
        if (flag_set(sr)) {
            if (p->flags[3] && flag_set(sr)) {
                /*read FIR filter parameters*/
                if (!decode_FIR_parameters(sr, &(p->channel[c].FIR))) {
                    return 0;
                }
            } else if (header_present) {
                /*default FIR filter parameters*/
                p->channel[c].FIR.shift = 0;
                a_int_reset(p->channel[c].FIR.coeff);
            }

            if (p->flags[2] && flag_set(sr)) {
                /*read IIR filter parameters*/
                if (!decode_IIR_parameters(sr, &(p->channel[c].IIR))) {
                    return 0;
                }
            } else if (header_present) {
                /*default IIR filter parameters*/
                p->channel[c].IIR.shift = 0;
                a_int_reset(p->channel[c].IIR.coeff);
                a_int_reset(p->channel[c].IIR.state);
            }

            if (p->flags[1] && flag_set(sr)) {
                p->channel[c].huffman_offset = sr->read_signed(sr, 15);
            } else if (header_present) {
                p->channel[c].huffman_offset = 0;
            }

            p->channel[c].codebook =
                sr->read(sr, 2);

            if ((p->channel[c].huffman_lsbs = sr->read(sr, 5)) > 24) {
                return 0;
            }

        } else if (header_present) {
            /*default channel parameters*/
            p->channel[c].FIR.shift = 0;
            a_int_reset(p->channel[c].FIR.coeff);
            p->channel[c].IIR.shift = 0;
            a_int_reset(p->channel[c].IIR.coeff);
            a_int_reset(p->channel[c].IIR.state);
            p->channel[c].huffman_offset = 0;
            p->channel[c].codebook = 0;
            p->channel[c].huffman_lsbs = 24;
        }
    }

    return 1;
}

static int
decode_matrix_parameters(BitstreamReader* sr,
                         unsigned max_matrix_channel,
                         unsigned* matrix_len,
                         struct matrix_parameters* mp)
{
    unsigned m;

    *matrix_len = sr->read(sr, 4);

    for (m = 0; m < *matrix_len; m++) {
        unsigned c;
        unsigned fractional_bits;

        if ((mp[m].out_channel =
             sr->read(sr, 4)) > max_matrix_channel)
            return 0;
        if ((fractional_bits =
             sr->read(sr, 4)) > 14)
            return 0;
        mp[m].LSB_bypass = flag_set(sr);
        for (c = 0; c < max_matrix_channel + 3; c++) {
            if (flag_set(sr)) {
                const int v = sr->read_signed(sr, fractional_bits + 2);
                mp[m].coeff[c] = v << (14 - fractional_bits);
            } else {
                mp[m].coeff[c] = 0;
            }
        }
    }

    return 1;
}

static int
decode_FIR_parameters(BitstreamReader* sr,
                      struct filter_parameters* FIR)
{
    const unsigned order = sr->read(sr, 4);

    if (order > 8) {
        return 0;
    } else if (order > 0) {
        unsigned coeff_bits;

        FIR->shift = sr->read(sr, 4);
        coeff_bits = sr->read(sr, 5);

        if ((1 <= coeff_bits) && (coeff_bits <= 16)) {
            const unsigned coeff_shift = sr->read(sr, 3);
            unsigned i;

            if ((coeff_bits + coeff_shift) > 16) {
                return 0;
            }

            FIR->coeff->reset(FIR->coeff);
            for (i = 0; i < order; i++) {
                const int v = sr->read_signed(sr, coeff_bits);
                FIR->coeff->append(FIR->coeff, v << coeff_shift);
            }
            if (flag_set(sr)) {
                return 0;
            }

            return 1;
        } else {
            return 0;
        }
    } else {
        FIR->shift = 0;
        FIR->coeff->reset(FIR->coeff);
        return 1;
    }
}

static int
decode_IIR_parameters(BitstreamReader* sr,
                      struct filter_parameters* IIR)
{
    const unsigned order = sr->read(sr, 4);

    if (order > 8) {
        return 0;
    } else if (order > 0) {
        unsigned coeff_bits;

        IIR->shift = sr->read(sr, 4);
        coeff_bits = sr->read(sr, 5);

        if ((1 <= coeff_bits) && (coeff_bits <= 16)) {
            const unsigned coeff_shift = sr->read(sr, 3);
            unsigned i;

            if ((coeff_bits + coeff_shift) > 16) {
                return 0;
            }

            IIR->coeff->reset(IIR->coeff);
            for (i = 0; i < order; i++) {
                const int v = sr->read_signed(sr, coeff_bits);
                IIR->coeff->append(IIR->coeff, v << coeff_shift);
            }
            IIR->state->reset(IIR->state);
            if (flag_set(sr)) {
                const unsigned state_bits = sr->read(sr, 4);
                const unsigned state_shift = sr->read(sr, 4);

                for (i = 0; i < order; i++) {
                    const int v = sr->read_signed(sr, state_bits);
                    IIR->state->append(IIR->state, v << state_shift);
                }
                IIR->state->reverse(IIR->state);
            }

            return 1;
        } else {
            return 0;
        }
    } else {
        IIR->shift = 0;
        IIR->coeff->reset(IIR->coeff);
        IIR->state->reset(IIR->state);
        return 1;
    }
}

static int
decode_residual_data(BitstreamReader* sr,
                     unsigned min_channel,
                     unsigned max_channel,
                     unsigned block_size,
                     unsigned matrix_len,
                     const struct matrix_parameters* matrix,
                     const unsigned* quant_step_size,
                     const struct channel_parameters* channel,
                     aa_int* residuals)
{
    int signed_huffman_offset[MAX_MLP_CHANNELS];
    unsigned LSB_bits[MAX_MLP_CHANNELS];
    static br_huffman_table_t mlp_codebook1[] =
#include "mlp_codebook1.h"
        ;
    static br_huffman_table_t mlp_codebook2[] =
#include "mlp_codebook2.h"
        ;
    static br_huffman_table_t mlp_codebook3[] =
#include "mlp_codebook3.h"
        ;

    unsigned c;
    unsigned m;
    unsigned i;

    /*calculate signed Huffman offset for each channel*/
    for (c = min_channel; c <= max_channel; c++) {
        LSB_bits[c] = channel[c].huffman_lsbs - quant_step_size[c];
        if (channel[c].codebook) {
            const int sign_shift = LSB_bits[c] + 2 - channel[c].codebook;
            if (sign_shift >= 0) {
                signed_huffman_offset[c] =
                    channel[c].huffman_offset -
                    (7 * (1 << LSB_bits[c])) -
                    (1 << sign_shift);
            } else {
                signed_huffman_offset[c] =
                    channel[c].huffman_offset -
                    (7 * (1 << LSB_bits[c]));
            }
        } else {
            const int sign_shift = LSB_bits[c] - 1;
            if (sign_shift >= 0) {
                signed_huffman_offset[c] =
                    channel[c].huffman_offset -
                    (1 << sign_shift);
            } else {
                signed_huffman_offset[c] = channel[c].huffman_offset;
            }
        }
    }

    /*reset residuals arrays*/
    residuals->reset(residuals);

    for (i = 0; i <= max_channel; i++) {
        /*residual channels 0 to "min_channel"
          will be initialized but not actually used*/
        a_int* channel = residuals->append(residuals);
        channel->resize(channel, block_size);
    }

    /*resize bypassed_LSB arrays for additional values*/
    for (m = 0; m < matrix_len; m++) {
        a_int* bypassed_LSB = matrix[m].bypassed_LSB;
        bypassed_LSB->resize(bypassed_LSB, bypassed_LSB->len + block_size);
    }

    for (i = 0; i < block_size; i++) {
        /*read bypassed LSBs for each matrix*/
        for (m = 0; m < matrix_len; m++) {
            a_int* bypassed_LSB = matrix[m].bypassed_LSB;
            if (matrix[m].LSB_bypass) {
                a_append(bypassed_LSB, sr->read(sr, 1));
            } else {
                a_append(bypassed_LSB, 0);
            }
        }

        /*read residuals for each channel*/
        for (c = min_channel; c <= max_channel; c++) {
            a_int* residual = residuals->_[c];
            register int MSB;
            register unsigned LSB;

            switch (channel[c].codebook) {
            case 0:
                MSB = 0;
                break;
            case 1:
                MSB = sr->read_huffman_code(sr, mlp_codebook1);
                break;
            case 2:
                MSB = sr->read_huffman_code(sr, mlp_codebook2);
                break;
            case 3:
                MSB = sr->read_huffman_code(sr, mlp_codebook3);
                break;
            default:
                MSB = -1;
                break;
            }
            if (MSB == -1)
                return 0;

            LSB = sr->read(sr, LSB_bits[c]);

            a_append(residual,
                     ((MSB << LSB_bits[c]) +
                      LSB +
                      signed_huffman_offset[c]) << quant_step_size[c]);
        }
    }

    return 1;
}

static int
filter_channel(const a_int* residuals,
               struct filter_parameters* FIR,
               struct filter_parameters* IIR,
               unsigned quant_step_size,
               a_int* filtered)
{
    const unsigned block_size = residuals->len;
    a_int* FIR_state = FIR->state;
    a_int* IIR_state = IIR->state;
    a_int* FIR_coeff = FIR->coeff;
    a_int* IIR_coeff = IIR->coeff;
    const int FIR_order = FIR->coeff->len;
    const int IIR_order = IIR->coeff->len;
    unsigned shift;
    int i;

    if ((FIR_order + IIR_order) > 8)
        return 0;
    if ((FIR->shift > 0) && (IIR->shift > 0)) {
        if (FIR->shift != IIR->shift)
            return 0;
        shift = FIR->shift;
    } else if (FIR_order > 0) {
        shift = FIR->shift;
    } else {
        shift = IIR->shift;
    }

    /*ensure arrays have enough space so we can use a_append*/
    FIR_state->resize(FIR_state, FIR_state->len + block_size);
    IIR_state->resize(IIR_state, IIR_state->len + block_size);
    filtered->reset(filtered);
    filtered->resize(filtered, block_size);

    for (i = 0; i < block_size; i++) {
        register int64_t sum = 0;
        int shifted_sum;
        int value;
        int j;
        int k;

        for (j = 0; j < FIR_order; j++)
            sum += (((int64_t)FIR_coeff->_[j] *
                     (int64_t)FIR_state->_[FIR_state->len - j  - 1]));

        for (k = 0; k < IIR_order; k++)
            sum += ((int64_t)IIR_coeff->_[k] *
                    (int64_t)IIR_state->_[IIR_state->len - k - 1]);

        shifted_sum = (int)(sum >> shift);

        value = mask(shifted_sum + residuals->_[i], quant_step_size);

        a_append(filtered, value);
        a_append(FIR_state, value);
        a_append(IIR_state, filtered->_[i] - shifted_sum);
    }

    FIR_state->tail(FIR_state, 8, FIR_state);
    IIR_state->tail(IIR_state, 8, IIR_state);

    return 1;
}

static void
rematrix_channels(aa_int* channels,
                  unsigned max_matrix_channel,
                  unsigned noise_shift,
                  unsigned* noise_gen_seed,
                  unsigned matrix_count,
                  const struct matrix_parameters* matrix,
                  const unsigned* quant_step_size)
{
    const unsigned block_size = channels->_[0]->len;
    aa_int* noise = aa_int_new();
    unsigned i;
    unsigned m;

    /*generate noise channels*/
    for (i = 0; i < 2; i++) {
        a_int* channel = noise->append(noise);
        channel->resize(channel, block_size);
    }
    for (i = 0; i < block_size; i++) {
        const unsigned shifted = (*noise_gen_seed >> 7) & 0xFFFF;
        a_append(noise->_[0],
                 ((int8_t)(*noise_gen_seed >> 15)) << noise_shift);
        a_append(noise->_[1],
                 ((int8_t)(shifted)) << noise_shift);
        *noise_gen_seed = (((*noise_gen_seed << 16) & 0xFFFFFFFF) ^
                           shifted ^ (shifted << 5));
    }

    /*perform channel rematrixing*/
    for (m = 0; m < matrix_count; m++) {
        for (i = 0; i < block_size; i++) {
            register int64_t sum = 0;
            unsigned c;
            for (c = 0; c <= max_matrix_channel; c++)
                sum += ((int64_t)channels->_[c]->_[i] *
                        (int64_t)matrix[m].coeff[c]);
            sum += ((int64_t)noise->_[0]->_[i] *
                    (int64_t)matrix[m].coeff[max_matrix_channel + 1]);
            sum += ((int64_t)noise->_[1]->_[i] *
                    (int64_t)matrix[m].coeff[max_matrix_channel + 2]);

            channels->_[matrix[m].out_channel]->_[i] =
                mask((int)(sum >> 14),
                     quant_step_size[matrix[m].out_channel]) +
                matrix[m].bypassed_LSB->_[i];
        }
    }

    noise->del(noise);
}

static void
checkdata_callback(uint8_t byte, struct checkdata *checkdata)
{
    const static uint8_t CRC8[] =
        {0x00, 0x63, 0xC6, 0xA5, 0xEF, 0x8C, 0x29, 0x4A,
         0xBD, 0xDE, 0x7B, 0x18, 0x52, 0x31, 0x94, 0xF7,
         0x19, 0x7A, 0xDF, 0xBC, 0xF6, 0x95, 0x30, 0x53,
         0xA4, 0xC7, 0x62, 0x01, 0x4B, 0x28, 0x8D, 0xEE,
         0x32, 0x51, 0xF4, 0x97, 0xDD, 0xBE, 0x1B, 0x78,
         0x8F, 0xEC, 0x49, 0x2A, 0x60, 0x03, 0xA6, 0xC5,
         0x2B, 0x48, 0xED, 0x8E, 0xC4, 0xA7, 0x02, 0x61,
         0x96, 0xF5, 0x50, 0x33, 0x79, 0x1A, 0xBF, 0xDC,
         0x64, 0x07, 0xA2, 0xC1, 0x8B, 0xE8, 0x4D, 0x2E,
         0xD9, 0xBA, 0x1F, 0x7C, 0x36, 0x55, 0xF0, 0x93,
         0x7D, 0x1E, 0xBB, 0xD8, 0x92, 0xF1, 0x54, 0x37,
         0xC0, 0xA3, 0x06, 0x65, 0x2F, 0x4C, 0xE9, 0x8A,
         0x56, 0x35, 0x90, 0xF3, 0xB9, 0xDA, 0x7F, 0x1C,
         0xEB, 0x88, 0x2D, 0x4E, 0x04, 0x67, 0xC2, 0xA1,
         0x4F, 0x2C, 0x89, 0xEA, 0xA0, 0xC3, 0x66, 0x05,
         0xF2, 0x91, 0x34, 0x57, 0x1D, 0x7E, 0xDB, 0xB8,
         0xC8, 0xAB, 0x0E, 0x6D, 0x27, 0x44, 0xE1, 0x82,
         0x75, 0x16, 0xB3, 0xD0, 0x9A, 0xF9, 0x5C, 0x3F,
         0xD1, 0xB2, 0x17, 0x74, 0x3E, 0x5D, 0xF8, 0x9B,
         0x6C, 0x0F, 0xAA, 0xC9, 0x83, 0xE0, 0x45, 0x26,
         0xFA, 0x99, 0x3C, 0x5F, 0x15, 0x76, 0xD3, 0xB0,
         0x47, 0x24, 0x81, 0xE2, 0xA8, 0xCB, 0x6E, 0x0D,
         0xE3, 0x80, 0x25, 0x46, 0x0C, 0x6F, 0xCA, 0xA9,
         0x5E, 0x3D, 0x98, 0xFB, 0xB1, 0xD2, 0x77, 0x14,
         0xAC, 0xCF, 0x6A, 0x09, 0x43, 0x20, 0x85, 0xE6,
         0x11, 0x72, 0xD7, 0xB4, 0xFE, 0x9D, 0x38, 0x5B,
         0xB5, 0xD6, 0x73, 0x10, 0x5A, 0x39, 0x9C, 0xFF,
         0x08, 0x6B, 0xCE, 0xAD, 0xE7, 0x84, 0x21, 0x42,
         0x9E, 0xFD, 0x58, 0x3B, 0x71, 0x12, 0xB7, 0xD4,
         0x23, 0x40, 0xE5, 0x86, 0xCC, 0xAF, 0x0A, 0x69,
         0x87, 0xE4, 0x41, 0x22, 0x68, 0x0B, 0xAE, 0xCD,
         0x3A, 0x59, 0xFC, 0x9F, 0xD5, 0xB6, 0x13, 0x70};

    checkdata->parity ^= byte;
    checkdata->crc = CRC8[(checkdata->final_crc = checkdata->crc ^ byte)];
}
