#ifndef AUDIO_TS
#define AUDIO_TS

/*a case-insensitive version of strcmp*/
int
strcmp_insensitive(const char *s1, const char *s2);

/*given a path to the AUDIO_TS directory
  and a filename to search for
  returns the full path to the file
  or NULL if the file is not found
  the path must be freed later once no longer needed

   filenames are compared case-insensitively*/
char*
find_audio_ts_file(const char* audio_ts_path, const char* filename);
#endif
