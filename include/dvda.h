struct DVDA_s;
struct DVDA_Titleset_s;
struct DVDA_Title_s;
struct DVDA_Track_s;
struct DVDA_Index_s;

typedef struct DVDA_s DVDA_t;
typedef struct DVDA_Titleset_s DVDA_Titleset_t;
typedef struct DVDA_Title_s DVDA_Title_t;
typedef struct DVDA_Track_s DVDA_Track_t;
typedef struct DVDA_Index_s DVDA_Index_t;

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

/*given a title set number (starting from 1)
  returns a DVDA_Titleset_t or NULL if ATS_XX_0.IFO is missing or invalid

  the DVDA_Titleset_t should be closed with dvda_close_titleset()
  when no longer needed*/
DVDA_Titleset_t*
dvda_open_titleset(DVDA_t* dvda, unsigned titleset);

/*closes the DVA_Titleset_t and deallocates any space it may have*/
void
dvda_close_titleset(DVDA_Titleset_t* titleset);

/*returns the number of titles in the title set*/
unsigned
dvda_title_count(const DVDA_Titleset_t* titleset);

/*given a title number (starting from 1)
  returns a DVDA_Title_t or NULL if the title is not found

  the DVDA_Title_t should be closed with dvda_close_title()
  when no longer needed*/
DVDA_Title_t*
dvda_open_title(DVDA_Titleset_t* titleset, unsigned title);

/*closes the DVDA_Title_t and deallocated any space it may have*/
void
dvda_close_title(DVDA_Title_t* title);

/*returns the number of tracks in the title*/
unsigned
dvda_track_count(const DVDA_Title_t* title);

unsigned
dvda_title_pts_length(const DVDA_Title_t* title);

/*given a track number (starting from 1)
  returns a DVDA_Track_t* or NULL if the track is not found

  the DVDA_Track_t should be closed with dvda_close_track()
  when no longer needed*/
DVDA_Track_t*
dvda_open_track(DVDA_Title_t* title, unsigned track);

void
dvda_close_track(DVDA_Track_t* track);

unsigned
dvda_track_pts_index(const DVDA_Track_t* track);

unsigned
dvda_track_pts_length(const DVDA_Track_t* track);

unsigned
dvda_track_first_sector(const DVDA_Title_t* title,
                        const DVDA_Track_t* track);

unsigned
dvda_track_last_sector(const DVDA_Title_t* title,
                       const DVDA_Track_t* track);

/*given an index number,
  returns a DVDA_Index_t* or NULL if the index is not found

  the DVDA_Index_t should be closed with dvda_close_index()
  when no longer needed*/
DVDA_Index_t*
dvda_open_index(DVDA_Title_t* title, unsigned index);

void
dvda_close_index(DVDA_Index_t* index);
