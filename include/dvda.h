struct DVDA_s;

typedef struct DVDA_s DVDA_t;

/*given a path to the disc's AUDIO_TS directory
  and a device (such as "/dev/cdrom") - which may be NULL,
  returns a DVDA_t or NULL if AUDIO_TS.IFO is missing or invalid

  the DVDA_t should be closed with dvda_close() when no longer needed*/
DVDA_t*
dvda_open(const char *audio_ts_path, const char *device);

/*closes the DVDA_t and deallocates any space it may have allocated*/
void
dvda_close(DVDA_t *dvda);

/*returns the number of title sets in the DVD-A*/
unsigned
dvda_titleset_count(const DVDA_t *dvda);
