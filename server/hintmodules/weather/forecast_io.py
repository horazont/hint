import functools
import itertools
import logging
import json
import socket
import urllib.error

from datetime import datetime, timedelta

import hintmodules
import hintmodules.utils
import hintmodules.errors
import hintmodules.weather
import hintmodules.weather.stanza as stanza
import hintmodules.caching_requester

from . import utils


def get_temperature(mapping, key, default=None):
    try:
        return utils.celsius_to_kelvin(mapping[key])
    except KeyError:
        return default


def get_float(mapping, key, default=None):
    try:
        return float(mapping[key])
    except KeyError:
        return default


class ForecastData:
    def __init__(self):
        self.datapoints = []
        self.time_index = {}

    def add_dp(self, dp):
        if dp.at in self.time_index:
            raise ValueError("Duplicate time: {}".format(dp.at))
        self.datapoints.append(dp)
        self.time_index[dp.at] = dp

    def sort(self):
        self.datapoints.sort(key=lambda x: x.at)


class ForecastDataPoint:
    at = None
    dewpoint = None
    pressure = None
    temperature = None
    wind_direction = None
    wind_speed = None
    humidity = None
    cloudiness = None
    fog = None
    cloudiness_low = None
    cloudiness_mid = None
    cloudiness_high = None
    precipitation_intensity = None
    precipitation_probability = None

    @classmethod
    def from_api(cls, api_datapoint):
        instance = cls()
        instance.at = datetime.utcfromtimestamp(api_datapoint["time"])
        instance.temperature = get_temperature(api_datapoint,
                                               "temperature")
        instance.dewpoint = get_temperature(api_datapoint,
                                            "dewpoint")
        instance.precipitation_intensity = get_float(api_datapoint,
                                                     "precipIntensity",
                                                     default=0.)
        instance.precipitation_probability = get_float(api_datapoint,
                                                       "precipProbability",
                                                       default=0.)
        instance.cloudiness = get_float(api_datapoint,
                                        "cloudCover",
                                        default=0.0) * 100.
        instance.wind_direction = get_float(api_datapoint,
                                            "windBearing")
        instance.wind_speed = get_float(api_datapoint,
                                        "windSpeed")
        instance.humidity = get_float(api_datapoint,
                                      "humidity",
                                      default=0.0) * 100.
        instance.nearest_storm_distance = get_float(api_datapoint,
                                                    "nearestStormDistance",
                                                    default=0.)

        return instance


class ForecastIORequester(hintmodules.caching_requester.AdvancedRequester):
    URL_TEMPLATE = "https://api.forecast.io/forecast/{apikey}/{lat},{lon}?units=si"

    def __init__(self, apikey):
        super().__init__(
            initial_back_off_time=timedelta(minutes=5))

        self.max_cache_over_expiry = timedelta(minutes=45)
        self.apikey = apikey
        self.user_agent = hintmodules.get_default_user_agent()

    def _is_too_stale(self, cache_entry):
        age = datetime.utcnow() - cache_entry.expires
        return age > self.max_cache_over_expiry

    def _get_backing_off_result(self, expired_cache_entry=None, **kwargs):
        # if the cache entry is not 'too old', return it, otherwise make it
        # explicit that we’re currently backing off.

        if expired_cache_entry is not None:
            now = datetime.utcnow()
            if self._is_too_stale(expired_cache_entry):
                return None

        return expired_cache_entry

    def _decode_response_into(self, response, cache_entry):
        try:
            contents = json.loads(response.read().decode())
        except ValueError:
            raise hintmodules.caching_requester.RequestError(
                str(err), back_off=False) from err

        try:
            expires = hintmodules.utils.parse_http_date(
                response.getheader("Expires"))
        except ValueError:
            expires = datetime.utcnow() + timedelta(minutes=7.5)

        cache_entry.data = self.parse_data(contents)
        cache_entry.expires = expires

    def _perform_request(self, expired_cache_entry, lat, lon):
        url = self.URL_TEMPLATE.format(
            apikey=self.apikey,
            lat=lat,
            lon=lon)

        try:
            response, _ = hintmodules.utils.http_request(
                url,
                user_agent=self.user_agent,
                accept="application/json")
        except (urllib.error.HTTPError,
                socket.timeout,
                urllib.error.URLError) as err:
            logging.warn("forecast.io request failed: {}".format(err))
            if self._is_too_stale(expired_cache_entry):
                cache_entry = None
            else:
                cache_entry = expired_cache_entry

            raise hintmodules.caching_requester.RequestError(
                str(err),
                back_off=True,
                cache_entry=cache_entry) from err
        else:
            cache_entry = (expired_cache_entry
                           or hintmodules.caching_requester.CacheEntry())

        try:
            self._decode_response_into(response, cache_entry)
        finally:
            response.close()

        return cache_entry

    def parse_data(self, data):
        result = ForecastData()
        for dp in itertools.chain(
                data.get("hourly", {}).get("data", []),
                data.get("daily", {}).get("data", [])):

            parsed_dp = ForecastDataPoint.from_api(dp)
            try:
                result.add_dp(parsed_dp)
            except ValueError:
                continue

        result.sort()

        return result


class ForecastIO:
    LICENSE = "CC-BY-SA"
    NAME = "forecast.io"

    def __init__(self, apikey):
        self.requester = ForecastIORequester(apikey)
        self._process_datapoint = {
            stanza.Temperature: self._dp_temperature,
            stanza.CloudCoverage: self._dp_cloudcoverage,
            stanza.Pressure: self._dp_pressure,
            stanza.Humidity: self._dp_humidity,
            stanza.WindDirection: self._dp_winddirection,
            stanza.WindSpeed: self._dp_windspeed,
            stanza.Precipitation: self._dp_precipitation,
            stanza.NearestStormDistance: self._dp_nearest_storm_distance,
        }

    def _populate_request_with_default(self, request, interval=True):
        request.append(stanza.Temperature(
            type=stanza.Temperature.Type.Air))
        request.append(stanza.Temperature(
            type=stanza.Temperature.Type.Dewpoint))
        request["wind_direction"]
        request["wind_speed"]
        request["pressure"]
        request["humidity"]
        request.append(stanza.CloudCoverage(
            level=stanza.CloudCoverage.Level.Overall))
        request["precipitation"]
        request["nearest_storm_distance"]

    def _dp_temperature(self, datapoint, node):
        value = {
            stanza.Temperature.Type.Air: datapoint.temperature,
            stanza.Temperature.Type.Dewpoint: datapoint.dewpoint
        }[node.get_type()]
        node.aggregate_value(value)

    def _dp_cloudcoverage(self, datapoint, node):
        value = {
            stanza.CloudCoverage.Level.Overall: datapoint.cloudiness,
        }[node.get_level()]
        node.aggregate_value(value)

    def _dp_pressure(self, datapoint, node):
        node.aggregate_value(datapoint.pressure)

    def _dp_humidity(self, datapoint, node):
        node.aggregate_value(datapoint.humidity)

    def _dp_winddirection(self, datapoint, node):
        node.aggregate_value(datapoint.wind_direction)

    def _dp_windspeed(self, datapoint, node):
        node.aggregate_value(datapoint.wind_speed)

    def _dp_precipitation(self, datapoint, node):
        node.aggregate_value(datapoint.precipitation_intensity)

    def _dp_nearest_storm_distance(self, datapoint, node):
        node.aggregate_value(datapoint.nearest_storm_distance)

    def _initialize_result_nodes(self, request):
        for node in hintmodules.utils.iter_all_plugins(request):
            if not hasattr(node, "set_aggregated_values"):
                node.set_value(0)

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

    def query_data_point(self, lat, lon, request):
        if len(request) == 0:
            self._populate_request_with_default(request)

        self._initialize_result_nodes(request)

        data = self.requester.request(lat=lat, lon=lon)
        at = request["at"]
        end = at + timedelta(seconds=1)

        self._aggregate_datapoints_for_request(
            request, data.datapoints, at, end)

        return request

    def query_interval(self, lat, lon, request):
        if len(request) == 0:
            self._populate_request_with_default(request)

        self._initialize_result_nodes(request)

        data = self.requester.request(lat=lat, lon=lon)
        start, end = request["start"], request["end"]

        self._aggregate_datapoints_for_request(
            request, data.datapoints, start, end)

        minv, avgv, maxv, std = request["precipitation"].get_aggregated_values()

        # total duration in hours
        duration = (end - start).total_seconds() / 3600.

        # precipitation intensity to total precipitation
        minv *= duration
        avgv *= duration
        maxv *= duration
        std *= duration

        request["precipitation"].set_aggregated_values(minv, avgv, maxv, std)

        return request
