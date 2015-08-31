import urllib.error
import socket
import logging
import functools
import itertools

from datetime import datetime, timedelta
import hintmodules.utils
import hintmodules.errors
import hintmodules.weather
import hintmodules.weather.stanza as stanza

import lxml.etree as ET

from . import utils

logger = logging.getLogger("weather.metno")

datefmt = "%Y-%m-%dT%H:%M:%SZ"

class MetNoParsers:
    @classmethod
    def _get_value(cls, xml, name, attr):
        try:
            return xml.find("location/"+name).get(attr)
        except AttributeError:
            return None

    @classmethod
    def temperature(cls, xml, key, default=None):
        valuestr = cls._get_value(xml, key, "value")
        if valuestr is None:
            return default
        else:
            return utils.celsius_to_kelvin(float(valuestr))

    @classmethod
    def value(cls, xml, name, attr, default=None):
        valuestr = cls._get_value(xml, name, attr)
        if valuestr is None:
            return default
        else:
            return float(valuestr)

    @classmethod
    def percent(cls, xml, key, default=None):
        return cls.value(xml, key, "percent")

@functools.total_ordering
class MetNoDataPoint:
    at = None

    # temperature in Kelvin
    temperature = None

    # direction in degrees from North
    wind_direction = None

    # wind speed in meter per second
    wind_speed = None

    # relative humidity in percent
    humidity = None

    # overall cloudiness in percent
    cloudiness = None

    # fog density in percent
    fog = None

    # cloudiness on different layers
    cloudiness_low = None
    cloudiness_mid = None
    cloudiness_high = None

    # dewpoint temperature in Kelvin
    dewpoint = None

    # pressure in hecto pascal
    pressure = None

    def __init__(self, metno_data_point_xml):
        xml = metno_data_point_xml
        start = datetime.strptime(xml.get("from"), datefmt)
        end = datetime.strptime(xml.get("to"), datefmt)
        if start != end:
            raise ValueError("Data point start and end are not equal")
        self.at = start
        self.temperature = MetNoParsers.temperature(xml, "temperature")
        self.dewpoint = MetNoParsers.temperature(xml, "dewpointTemperature")
        self.cloudiness = MetNoParsers.percent(xml, "cloudiness")
        self.cloudiness_low = MetNoParsers.percent(xml, "lowClouds")
        self.cloudiness_mid = MetNoParsers.percent(xml, "mediumClouds")
        self.cloudiness_high = MetNoParsers.percent(xml, "highClouds")
        self.fog = MetNoParsers.percent(xml, "fog")
        self.humidity = MetNoParsers.value(xml, "humidity", "value")
        self.wind_speed = MetNoParsers.value(xml, "windSpeed", "mps")
        self.wind_direction = MetNoParsers.value(xml, "windDirection", "deg")
        self.pressure = MetNoParsers.value(xml, "pressure", "value")

    def __lt__(self, other):
        return self.at < other.at

    def __repr__(self):
        return "<met.no datapoint at {}>".format(self.at)

class MetNoInterval:
    start = None
    end = None

    # precipitation in millimeters
    precipitation = None

    def __init__(self, metno_interval_xml):
        xml = metno_interval_xml
        self.start = datetime.strptime(xml.get("from"), datefmt)
        self.end = datetime.strptime(xml.get("to"), datefmt)
        self.precipitation = MetNoParsers.value(xml, "precipitation", "value")

    def __lt__(self, other):
        if self.start == other.start:
            return self.end < other.end
        else:
            return self.start < other.start

    def __repr__(self):
        return "<met.no interval from {} until {}>".format(
            self.start, self.end)

class Weather:
    URL = "http://api.met.no/weatherapi/locationforecast/1.8/?lat={lat}&lon={lon}"
    MAX_AGE = timedelta(seconds=60*30)

    LICENSE = "CC-BY-SA"
    NAME = "Norwegian Meteorological Institute"

    def __init__(self, lat, lon, user_agent="Weather/1.0"):
        self.url = self.URL.format(lat=lat, lon=lon)
        self.user_agent = user_agent
        self.cached_data = None
        self.cached_timestamp = None
        self._process_datapoint = {
            stanza.Temperature: self._dp_temperature,
            stanza.CloudCoverage: self._dp_cloudcoverage,
            stanza.Pressure: self._dp_pressure,
            stanza.Fog: self._dp_fog,
            stanza.Humidity: self._dp_humidity,
            stanza.WindDirection: self._dp_winddirection,
            stanza.WindSpeed: self._dp_windspeed
        }
        self._process_interval = {
            stanza.Precipitation: self._iv_precipitation
        }

    def _get_raw_xml(self):
        response, timestamp = hintmodules.utils.http_request(
            self.url,
            user_agent=self.user_agent,
            last_modified=self.cached_timestamp,
            accept="application/xml")
        # response, timestamp = open("/home/horazont/tmp/metno.xml", "rb"), datetime.utcnow()
        try:
            contents = response.read().decode()
            return contents, timestamp
        finally:
            response.close()

    def _not_available(self):
        return hintmodules.errors.ServiceNotAvailable(self.NAME)

    def _has_cache(self):
        if not self.cached_data:
            return False
        return self.cache_timestamp + self.MAX_AGE > datetime.utcnow()

    def _return_cache_or_raise(self, err_context):
        logger.warn("error during request: %s", err_context)
        if self._has_cache():
            logger.info("cache is invalid, not returning an error")
            return self.cached_data
        raise self._not_available() from err_context

    def parse_xml(self, tree):
        if not tree.xpath("/weatherdata"):
            raise ValueError("Tree is not a valid weatherdata tree")

        data_points = []
        for xml in tree.xpath("//time[@datatype='forecast' and @from=@to]"):
            data = MetNoDataPoint(xml)
            data_points.append(data)
        data_points.sort()

        intervals = []
        for xml in tree.xpath("//time[@datatype='forecast' and @from!=@to]"):
            interval = MetNoInterval(xml)
            intervals.append(interval)
        intervals.sort()

        return data_points, intervals

    def get_data(self):
        try:
            xml, timestamp = self._get_raw_xml()
        except urllib.error.HTTPError as err:
            if err.code == 304:
                return self.cached_data
            return self._return_cache_or_raise(err)
        except socket.timeout as err:
            return self._return_cache_or_raise(err)
        except urllib.error.URLError as err:
            return self._return_cache_or_raise(err)

        xml = ET.ElementTree(ET.fromstring(xml))
        try:
            self.cached_data = self.parse_xml(xml)
        except ValueError as err:
            return self._return_cache_or_raise(err)
        self.cached_timestamp = timestamp
        return self.cached_data

    def _populate_request_with_default(self, request, interval=True):
        request.append(stanza.Temperature(
            type=stanza.Temperature.Type.Air))
        request.append(stanza.Temperature(
            type=stanza.Temperature.Type.Dewpoint))
        request["wind_direction"]
        request["wind_speed"]
        request["pressure"]
        request["humidity"]
        request["fog"]
        request.append(stanza.CloudCoverage(
            level=stanza.CloudCoverage.Level.Overall))
        request.append(stanza.CloudCoverage(
            level=stanza.CloudCoverage.Level.Low))
        request.append(stanza.CloudCoverage(
            level=stanza.CloudCoverage.Level.Medium))
        request.append(stanza.CloudCoverage(
            level=stanza.CloudCoverage.Level.High))
        if interval:
            request["precipitation"]

    def _dp_temperature(self, datapoint, node):
        value = {
            stanza.Temperature.Type.Air: datapoint.temperature,
            stanza.Temperature.Type.Dewpoint: datapoint.dewpoint
        }[node.get_type()]
        node.aggregate_value(value)

    def _dp_cloudcoverage(self, datapoint, node):
        value = {
            stanza.CloudCoverage.Level.Overall: datapoint.cloudiness,
            stanza.CloudCoverage.Level.Low: datapoint.cloudiness_low,
            stanza.CloudCoverage.Level.Medium: datapoint.cloudiness_mid,
            stanza.CloudCoverage.Level.High: datapoint.cloudiness_high
        }[node.get_level()]
        node.aggregate_value(value)

    def _dp_pressure(self, datapoint, node):
        node.aggregate_value(datapoint.pressure)

    def _dp_fog(self, datapoint, node):
        node.aggregate_value(datapoint.fog)

    def _dp_humidity(self, datapoint, node):
        node.aggregate_value(datapoint.humidity)

    def _dp_winddirection(self, datapoint, node):
        node.aggregate_value(datapoint.wind_direction)

    def _dp_windspeed(self, datapoint, node):
        node.aggregate_value(datapoint.wind_speed)

    def _iv_precipitation(self, interval, node):
        node.set_value(node.get_value() + interval.precipitation)

    def _aggregate_datapoints_for_request(
            self, request, datapoints, start, end):
        matching = list(itertools.takewhile(
            lambda x: x.at < end,
            itertools.dropwhile(
                lambda x: x.at < start,
                datapoints)))

        if not matching:
            raise ValueError("No datapoints for given interval")

        handlers = [
            (self._process_datapoint[node.__class__], node)
            for node in hintmodules.utils.iter_all_plugins(request)
            if node.__class__ in self._process_datapoint
        ]

        for datapoint in matching:
            for handler, node in handlers:
                handler(datapoint, node)

    def _aggregate_intervals_for_request(
            self, request, intervals, start, end):
        matching = list(filter(
                lambda x: x.end <= end,
                itertools.takewhile(
                    lambda x: x.start <= end,
                    itertools.dropwhile(
                        lambda x: x.start < start,
                        intervals))))

        if not matching:
            raise ValueError("No interval data for given interval")

        if matching[0].start != start:
            raise ValueError("No matching interval data for given interval")

        handlers = [
            (self._process_interval[node.__class__], node)
            for node in hintmodules.utils.iter_all_plugins(request)
            if node.__class__ in self._process_interval
        ]

        grouped_matches = itertools.groupby(
            matching, lambda x: x.start)

        currstart = None
        for interval_start, intervals in grouped_matches:
            if currstart is not None and interval_start < currstart:
                continue
            intervals = sorted(intervals, reverse=True)
            for candidate in intervals:
                if candidate.end <= end:
                    currstart = candidate.end
                    for handler, node in handlers:
                        handler(candidate, node)
                    break
            else:
                raise ValueError("No matching interval data for given interval")

    def _initialize_result_nodes(self, request):
        for node in hintmodules.utils.iter_all_plugins(request):
            if not hasattr(node, "set_aggregated_values"):
                node.set_value(0)

    def query_data_point(self, lat, lon, request):
        if len(request) == 0:
            self._populate_request_with_default(request, False)
        self._initialize_result_nodes(request)
        datapoints, intervals = self.get_data()
        at = request["at"]
        end = at + timedelta(seconds=1)

        self._aggregate_datapoints_for_request(
            request, datapoints, at, end)

        return request

    def query_interval(self, lat, lon, request):
        if len(request) == 0:
            # no children, we provide all information we can
            self._populate_request_with_default(request)
        self._initialize_result_nodes(request)
        datapoints, intervals = self.get_data()
        start, end = request["start"], request["end"]

        self._aggregate_datapoints_for_request(
            request, datapoints, start, end)
        self._aggregate_intervals_for_request(
            request, intervals, start, end)

        return request
