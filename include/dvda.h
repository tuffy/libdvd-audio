struct DVDA_s;
struct DVDA_Titleset_s;
struct DVDA_Title_s;
struct DVDA_Track_s;
struct DVDA_Index_s;
struct DVDA_Titleset_Reader_s;
struct DVDA_Title_Reader_s;

typedef struct DVDA_s DVDA;
typedef struct DVDA_Titleset_s DVDA_Titleset;
typedef struct DVDA_Title_s DVDA_Title;
typedef struct DVDA_Track_s DVDA_Track;
typedef struct DVDA_Index_s DVDA_Index;
typedef struct DVDA_Titleset_Reader_s DVDA_Titleset_Reader;
typedef struct DVDA_Title_Reader_s DVDA_Title_Reader;

typedef enum {DVDA_PCM, DVDA_MLP} dvda_codec_t;

/*given a path to the disc's AUDIO_TS directory
  and a device (such as "/dev/cdrom") - which may be NULL,
  returns a DVDA or NULL if AUDIO_TS.IFO is missing or invalid

  the DVDA should be closed with dvda_close() when no longer needed*/
DVDA*
dvda_open(const char *audio_ts_path, const char *device);

/*closes the DVDA and deallocates any space it may have allocated*/
void
dvda_close(DVDA *dvda);

/*returns the number of title sets in the DVD-A*/
unsigned
dvda_titleset_count(const DVDA *dvda);

/*given a title set number (starting from 1)
  returns a DVDA_Titleset or NULL if ATS_XX_0.IFO is missing or invalid

  the DVDA_Titleset should be closed with dvda_close_titleset()
  when no longer needed*/
DVDA_Titleset*
dvda_open_titleset(DVDA* dvda, unsigned titleset);

/*closes the DVA_Titleset and deallocates any space it may have*/
void
dvda_close_titleset(DVDA_Titleset* titleset);

/*returns the number of titles in the title set*/
unsigned
dvda_title_count(const DVDA_Titleset* titleset);

/*given a title set number (starting from 1)
  return a DVDA_Titleset_Reader or NULL if the AOB files
  are missing or invalid

  the DVDA_Titleset_Reader should be closed with dvda_close_titleset_reader()
  when no longer needed*/
DVDA_Titleset_Reader*
dvda_open_titleset_reader(DVDA* dvda, unsigned titleset);

void
dvda_close_titleset_reader(DVDA_Titleset_Reader* reader);

/*given a title number (starting from 1)
  returns a DVDA_Title or NULL if the title is not found

  the DVDA_Title should be closed with dvda_close_title()
  when no longer needed*/
DVDA_Title*
dvda_open_title(DVDA_Titleset* titleset, unsigned title);

void
dvda_close_title(DVDA_Title* title);

/*returns the number of tracks in the title*/
unsigned
dvda_track_count(const DVDA_Title* title);

/*returns the total length of the title in PTS ticks*/
unsigned
dvda_title_pts_length(const DVDA_Title* title);

/*given a title number (starting from 1)
  returns a DVDA_Titlte_Reader or NULL if the title is not found

  the DVDA_Title_Reader should be closed with dvda_close_title_reader()
  when no longer needed*/
DVDA_Title_Reader*
dvda_open_title_reader(DVDA_Titleset_Reader* titleset_reader, unsigned title);

void
dvda_close_title_reader(DVDA_Title_Reader* title_reader);

dvda_codec_t
dvda_codec(DVDA_Title_Reader* reader);

unsigned
dvda_bits_per_sample(DVDA_Title_Reader* reader);

unsigned
dvda_sample_rate(DVDA_Title_Reader* reader);

unsigned
dvda_channel_count(DVDA_Title_Reader* reader);

unsigned
dvda_channel_assignment(DVDA_Title_Reader* reader);

/*given a buffer with at least channel_count * pcm_frames integers,
  populates that buffer with as many samples as possible
  interleaved on a per-channel basis
  (left[0], right[0], left[1], right[1], ...)
  in DVD-A channel order

  returns the number of PCM frames actually read
  which may be less than requested at the end of the stream*/
unsigned
dvda_read(DVDA_Title_Reader* reader,
          unsigned pcm_frames,
          int buffer[]);

/*given a track number (starting from 1)
  returns a DVDA_Track* or NULL if the track is not found

  the DVDA_Track should be closed with dvda_close_track()
  when no longer needed*/
DVDA_Track*
dvda_open_track(DVDA_Title* title, unsigned track);

void
dvda_close_track(DVDA_Track* track);

/*returns the index of the track in PTS ticks*/
unsigned
dvda_track_pts_index(const DVDA_Track* track);

/*returns the total length of the track in PTS ticks*/
unsigned
dvda_track_pts_length(const DVDA_Track* track);

/*returns the track's first sector
  it may not start at the very beginning of the sector*/
unsigned
dvda_track_first_sector(const DVDA_Title* title,
                        const DVDA_Track* track);

/*returns the track's last sector
  it may not end at the very end of the sector*/
unsigned
dvda_track_last_sector(const DVDA_Title* title,
                       const DVDA_Track* track);
