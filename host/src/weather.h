#ifndef _WEATHER_H
#define _WEATHER_H

struct weather_interval_t {
    time_t start;
    time_t end;
    float temperature_celsius;
    float humidity_percent;
    float windspeed_meter_per_second;
    float cloudiness_percent;
    float precipitation_millimeter;
};

#endif
