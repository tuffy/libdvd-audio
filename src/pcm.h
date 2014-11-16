#include <stdint.h>
#include "array.h"
#include "bitstream.h"
#include "stream_parameters.h"

struct PCMDecoder_s;

typedef struct PCMDecoder_s PCMDecoder;

PCMDecoder*
dvda_open_pcmdecoder(unsigned bits_per_sample, unsigned channel_count);

void
dvda_close_pcmdecoder(PCMDecoder* decoder);

/*decodes the PCM stream parameters from the start of the packet*/
void
dvda_pcmdecoder_decode_params(BitstreamReader *packet_reader,
                              struct stream_parameters* parameters);

/*given a packet reader substream
  (not including the stream parameters or second padding)
  decodes as many samples as possible to samples
  and returns the number of PCM frames decoded*/
unsigned
dvda_pcmdecoder_decode_packet(PCMDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples);
