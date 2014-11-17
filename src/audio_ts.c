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

#include "audio_ts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

int
strcmp_insensitive(const char *s, const char *t)
{
    size_t i;
    for (i = 0; toupper(s[i]) == toupper(t[i]); i++) {
        if (s[i] == '\0')
            return 0;
    }
    return toupper(s[i]) - toupper(t[i]);
}

char*
find_audio_ts_file(const char* audio_ts_path, const char* filename)
{
    DIR* dir = opendir(audio_ts_path);
    struct dirent* dirent;
    if (!dir) {
        return NULL;
    }

    for (dirent = readdir(dir); dirent != NULL; dirent = readdir(dir)) {
        /*convert directory entry to upper-case*/
        if (!strcmp_insensitive(filename, dirent->d_name)) {
            /*if the filename matches,
              join audio_ts path and name into a single path
              and return it*/

            const size_t dirent_len = strlen(dirent->d_name);

            const size_t full_path_len = (strlen(audio_ts_path) +
                                          1 + /*path separator*/
                                          dirent_len +
                                          1 /*NULL*/);
            char* full_path = malloc(full_path_len);

            snprintf(full_path, full_path_len,
                     "%s/%s", audio_ts_path, dirent->d_name);

            closedir(dir);
            return full_path;
        }
    }

    /*gone through entire directory without a match*/
    closedir(dir);
    return NULL;
}
