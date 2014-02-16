#ifndef _DEPARTURE_H
#define _DEPARTURE_H

#define DEPARTURE_LANE_LENGTH (4)

struct dept_row_t {
    char lane[DEPARTURE_LANE_LENGTH+1];
    char *destination;
    int eta;
};

#endif
