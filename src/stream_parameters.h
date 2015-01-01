/********************************************************
 DVD-A Library, a module for reading DVD-Audio discs
 Copyright (C) 2014-2015  Brian Langenberger

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

#ifndef __LIBDVDAUDIO_STREAM_PARAMETERS_H__
#define __LIBDVDAUDIO_STREAM_PARAMETERS_H__

struct stream_parameters {
    unsigned group_0_bps;
    unsigned group_1_bps;
    unsigned group_0_rate;
    unsigned group_1_rate;
    unsigned channel_assignment;
};

static inline int
dvda_params_equal(const struct stream_parameters *p1,
                  const struct stream_parameters *p2)
{
    return ((p1->group_0_bps == p2->group_0_bps) &&
            (p1->group_1_bps == p2->group_1_bps) &&
            (p1->group_0_rate == p2->group_0_rate) &&
            (p1->group_1_rate == p2->group_1_rate) &&
            (p1->channel_assignment == p2->channel_assignment));
}

#endif
