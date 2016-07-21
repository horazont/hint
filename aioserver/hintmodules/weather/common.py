from enum import Enum


class PrecipitationType(Enum):
    RAIN = "rain"
    SNOW = "snow"
    SLEET = "sleet"


class Interval:
    precipitation = None
    precipitation_probability = None
    precipitation_type = None
    precipitation_accumulation = None
    precipitation_min = None
    precipitation_max = None

    def __init__(self, start, end):
        self.start = start
        self.end = end

    def __repr__(self):
        data = [
            ("precipitation", self.precipitation),
            ("precipitation_probability", self.precipitation_probability),
            ("precipitation_type", self.precipitation_type),
            ("precipitation_accumulation", self.precipitation_accumulation),
            ("precipitation_min", self.precipitation_min),
            ("precipitation_max", self.precipitation_max),
        ]

        return "<{}.{}({!r}, {!r}) {}>".format(
            type(self).__module__,
            type(self).__qualname__,
            self.start,
            self.end,
            " ".join(
                "{}={!r}".format(key, value)
                for key, value in data
                if value is not None
            )
        )


class Timepoint:
    apparent_temperature = None
    cloud_cover = None
    cloud_cover_low = None
    cloud_cover_mid = None
    cloud_cover_high = None
    dewpoint_temperature = None
    fog = None
    humidity = None
    nearest_storm_bearing = None
    nearest_storm_distance = None
    ozone = None
    pressure = None
    temperature = None
    visibility = None
    wind_speed = None
    wind_bearing = None

    def __init__(self, timestamp):
        self.timestamp = timestamp

    def __repr__(self):
        data = [
            ("apparent_temperature", self.apparent_temperature),
            ("cloud_cover", self.cloud_cover),
            ("cloud_cover_low", self.cloud_cover_low),
            ("cloud_cover_mid", self.cloud_cover_mid),
            ("cloud_cover_high", self.cloud_cover_high),
            ("dewpoint_temperature", self.dewpoint_temperature),
            ("fog", self.fog),
            ("humidity", self.humidity),
            ("nearest_storm_bearing", self.nearest_storm_bearing),
            ("nearest_storm_distance", self.nearest_storm_distance),
            ("ozone", self.ozone),
            ("pressure", self.pressure),
            ("temperature", self.temperature),
            ("visibility", self.visibility),
            ("wind_speed", self.wind_speed),
            ("wind_bearing", self.wind_bearing),
        ]

        return "<{}.{}({!r}) {}>".format(
            type(self).__module__,
            type(self).__qualname__,
            self.timestamp,
            " ".join(
                "{}={!r}".format(key, value)
                for key, value in data
                if value is not None
            )
        )
