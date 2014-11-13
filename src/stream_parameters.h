#ifndef STREAM_PARAMETERS
#define STREAM_PARAMETERS

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
