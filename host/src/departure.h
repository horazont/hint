#ifndef _DEPARTURE_H
#define _DEPARTURE_H

struct dept_row_t {
    char lane[3];
    char *destination;
    int remaining_time;
};

#endif
