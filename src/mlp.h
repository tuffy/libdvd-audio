#include <stdint.h>
#include "array.h"
#include "bitstream.h"
#include "stream_parameters.h"

struct MLPDecoder_s;

typedef struct MLPDecoder_s MLPDecoder;

MLPDecoder*
dvda_open_mlpdecoder(const struct stream_parameters* parameters);

void
dvda_close_mlpdecoder(MLPDecoder* decoder);

/*given a packet reader substream
  (not including the header or pad 2 bytes)
  decodes as many samples as possible to samples
  and returns the number of PCM frames decoded*/
unsigned
dvda_mlpdecoder_decode_packet(MLPDecoder* decoder,
                              BitstreamReader* packet_reader,
                              aa_int* samples);
