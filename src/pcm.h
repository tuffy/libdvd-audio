#include <stdint.h>
#include "array.h"
#include "bitstream.h"
#include "stream_parameters.h"

struct PCMDecoder_s;

typedef struct PCMDecoder_s PCMDecoder;

PCMDecoder*
dvda_open_pcmdecoder(const struct stream_parameters* parameters,
                     unsigned bits_per_sample,
                     unsigned channel_count);

void
dvda_close_pcmdecoder(PCMDecoder* decoder);

/*given a 2048 byte sector of data,
  decodes as many samples as possible to samples
  and returns the number of PCM frames decoded

  may return 0 at the end of stream or if a read error occurs*/
unsigned
dvda_pcmdecoder_decode_sector(PCMDecoder* decoder,
                              const uint8_t* sector,
                              aa_int* samples);

/*decodes all leftover data to samples
  and returns the number of PCM frames decoded

  may return 0 at the end of stream or if a read error occurs*/
unsigned
dvda_pcmdecoder_flush(PCMDecoder* decoder,
                      aa_int* samples);

void
dvda_pcmdecoder_decode_params(BitstreamReader *sector_reader,
                              struct stream_parameters* parameters);
