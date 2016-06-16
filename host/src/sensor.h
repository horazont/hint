#ifndef _SENSOR_H
#define _SENSOR_H

#include <time.h>

#define MAX_READOUTS_IN_BATCH (8)
#define MAX_BATCHES           (256)

struct sensor_readout_t {
    time_t readout_time;
    uint8_t sensor_id[7];
    int16_t raw_value;
};

struct sensor_readout_batch_t {
    struct sensor_readout_t data[MAX_READOUTS_IN_BATCH];
    int write_offset;
};

#endif
