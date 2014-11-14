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

unsigned
dvda_mlpdecoder_decode_sector(MLPDecoder* decoder,
                              const uint8_t sector[],
                              aa_int* samples);

unsigned
dvda_mlpdecoder_flush(MLPDecoder* decoder,
                      aa_int* samples);
