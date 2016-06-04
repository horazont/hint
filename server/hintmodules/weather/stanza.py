from datetime import datetime, timedelta
from sleekxmpp import Iq
from sleekxmpp.xmlstream import register_stanza_plugin, ElementBase

xmlns = "https://xmlns.zombofant.net/xmpp/meteo-service"
datefmt = "%Y-%m-%dT%H:%M:%SZ"

class AttributeContainer(ElementBase):
    namespace = xmlns

class Data(ElementBase):
    namespace = xmlns
    name = "data"
    plugin_attrib = "weather_data"
    interfaces = set(
        ("from", "processed", "fetch"))

class Sources(ElementBase):
    namespace = xmlns
    name = "sources"
    plugin_attrib = "weather_sources"
    interfaces = set()

class Source(AttributeContainer):
    name = "source"
    plugin_attrib = name
    interfaces = set(
        ("uri", "license", "name", "serves"))

class Location(ElementBase):
    namespace = xmlns
    name = "l"
    plugin_attrib = "location"
    interfaces = set(
        ("lat", "lon"))

    def get_lat(self):
        return float(self._get_attr("lat"))

    def get_lon(self):
        return float(self._get_attr("lon"))

    def set_lat(self, value):
        self._set_attr("lat", "{:.4f}".format(value))

    def set_lon(self, value):
        self._set_attr("lon", "{:.4f}".format(value))

class Interval(AttributeContainer):
    name = "i"
    plugin_attrib = "interval"
    interfaces = set(
        ("start", "end"))

    def __init__(self, start=None, end=None, xml=None, parent=None):
        super().__init__(xml=xml, parent=parent)
        if start is not None:
            self.set_start(start)
        if end is not None:
            self.set_end(end)

    def get_end(self):
        return datetime.strptime(self._get_attr("end"), datefmt)

    def get_start(self):
        return datetime.strptime(self._get_attr("start"), datefmt)

    def set_end(self, dt):
        self._set_attr("end", dt.strftime(datefmt))

    def set_start(self, dt):
        self._set_attr("start", dt.strftime(datefmt))

class PointData(AttributeContainer):
    name = "pd"
    plugin_attrib = "point_data"
    interfaces = set(("at",))

    def get_at(self):
        return datetime.strptime(self._get_attr("at"), datefmt)

    def set_at(self, dt):
        self._set_attr("at", dt.strftime(datefmt))

class AggregatedValuesMixin:
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.clear_aggregate()

    def clear_aggregate(self):
        self.aggregate_buffer = []

    def aggregate_value(self, value):
        if value is None:
            return
        self.aggregate_buffer.append(value)
        self.transfer_aggregate(False)

    def transfer_aggregate(self, clear=True):
        if not self.aggregate_buffer:
            return

        buffer = self.aggregate_buffer
        minv = min(buffer)
        maxv = max(buffer)
        avg = sum(buffer) / len(buffer)

        self.set_aggregated_values(minv, avg, maxv)
        if clear:
            self.aggregate_buffer = []

    def del_aggregated_values(self):
        self._del_attr("min")
        self._del_attr("v")
        self._del_attr("max")
        self._del_attr("std")

    def get_aggregated_values(self):
        return tuple(map(float, (
            self._get_attr("min", "NaN"),
            self._get_attr("v", "NaN"),
            self._get_attr("max", "NaN"),
            self._get_attr("std", "NaN"))))

    def set_aggregated_values(self, *args, std=None):
        if len(args) == 3:
            min, avg, max = args
        elif len(args) == 4:
            if std is not None:
                raise TypeError("set_aggregated_values() got multiple values for argument 'std'")
            min, avg, max, std = args
        elif len(args) == 1:
            return self.set_aggregated_values(*args[0], std=std)

        self._set_attr("min", "{:.2f}".format(min))
        self._set_attr("v", "{:.2f}".format(avg))
        self._set_attr("max", "{:.2f}".format(max))
        if std is not None:
            self._set_attr("std", "{:.2f}".format(std))
        else:
            self._del_attr("std")
        self._del_attr("value")

class SingleValueMixin:
    def del_value(self):
        self._del_attr("v")

    def get_value(self):
        return float(self._get_attr("v"))

    def set_value(self, value):
        self._del_attr("min")
        self._del_attr("max")
        self._del_attr("std")
        self._set_attr("v", "{:.2f}".format(value))

class Temperature(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "t"
    plugin_attrib = "temperature"
    interfaces = set(
        ("type", "aggregated_values", "value"))

    class Type:
        __init__ = None

        Air = "air"
        Dewpoint = "dewpoint"

    def __init__(self, type=None, xml=None, parent=None):
        super().__init__(xml=xml, parent=parent)
        if type is not None:
            self.set_type(type)

    def get_type(self):
        return self._get_attr("t", "")

    def set_type(self, type_):
        self._set_attr("t", str(type_))

class CloudCoverage(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "cc"
    plugin_attrib = "cloud_coverage"
    interfaces = set(
        ("level", "aggregated_values", "value"))

    class Level:
        __init__ = None

        Overall = "all"
        Low = "low"
        Mid = "mid"
        Medium = Mid
        Middle = Mid
        High = "high"

    def __init__(self, level=None, xml=None, parent=None):
        super().__init__(xml=xml, parent=parent)
        if level is not None:
            self.set_level(level)

    def del_level(self):
        self._set_attr("lvl", "a")

    def get_level(self):
        return self._get_attr("lvl")

    def set_level(self, value):
        self._set_attr("lvl", value)

class Pressure(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "press"
    plugin_attrib = "pressure"
    interfaces = set(
        ("aggregated_values", "value"))

class Fog(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "f"
    plugin_attrib = "fog"
    interfaces = set(
        ("aggregated_values", "value"))

class Humidity(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "h"
    plugin_attrib = "humidity"
    interfaces = set(
        ("aggregated_values", "value"))

class WindDirection(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "wd"
    plugin_attrib = "wind_direction"
    interfaces = set(
        ("aggregated_values", "value"))

class WindSpeed(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "ws"
    plugin_attrib = "wind_speed"
    interfaces = set(
        ("aggregated_values", "value"))

class Precipitation(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "prec"
    plugin_attrib = "precipitation"
    interfaces = set(("aggregated_values", "value",))

class NearestStormDistance(AggregatedValuesMixin, SingleValueMixin, ElementBase):
    namespace = xmlns
    name = "nsd"
    plugin_attrib = "nearest_storm_distance"
    interfaces = set(("aggregated_values", "value",))

register_stanza_plugin(Iq, Data)
register_stanza_plugin(Iq, Sources)

register_stanza_plugin(Sources, Source, iterable=True)

register_stanza_plugin(Data, Location)
register_stanza_plugin(Data, Interval, iterable=True)
register_stanza_plugin(Data, PointData, iterable=True)

register_stanza_plugin(AttributeContainer, Temperature, iterable=True)
register_stanza_plugin(AttributeContainer, CloudCoverage, iterable=True)
register_stanza_plugin(AttributeContainer, Pressure)
register_stanza_plugin(AttributeContainer, WindDirection)
register_stanza_plugin(AttributeContainer, WindSpeed)
register_stanza_plugin(AttributeContainer, Fog)
register_stanza_plugin(AttributeContainer, Precipitation)
register_stanza_plugin(AttributeContainer, Humidity)
register_stanza_plugin(AttributeContainer, NearestStormDistance)
