#include <stdint.h>

struct AOB_Reader_s;

typedef struct AOB_Reader_s AOB_Reader;

/*given a full path to an AUDIO_TS directory
  cdrom device (or NULL)
  and title set number (starting from 1),
  returns an AOB_Reader or NULL if an error occured*/
AOB_Reader*
aob_reader_open(const char *audio_ts_path,
                const char *cdrom_device,
                unsigned titleset);

/*closes an opened reader*/
void
aob_reader_close(AOB_Reader *reader);

/*given a reader and sector buffer,
  reads exactly 2048 bytes to that buffer
  and returns 0 on success, 1 on failure*/
int
aob_reader_read(AOB_Reader *reader, uint8_t *sector_data);

/*seeks to the given sector number
  and returns 0 on success, 1 on failure*/
int
aob_reader_seek(AOB_Reader *reader, unsigned sector_number);
