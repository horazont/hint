import ast
import abc
from datetime import datetime, timedelta
import socket
import warnings
import urllib.error

import hintmodules.utils

def get_timestamp():
    import calendar
    return calendar.timegm(datetime.utcnow().utctimetuple())

class StopFilter(object, metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def filter_departures(self, input):
        return input

class StopFilterFunc(StopFilter):
    def __init__(self, filter_func):
        self.filter_func = filter_func

    def _filter_departure(self, dep):
        return self.filter_func(dep[0])

    def filter_departures(self, input):
        return list(filter(self._filter_departure, input))

class Departure(object):
    URL = "http://widgets.vvo-online.de/abfahrtsmonitor/Abfahrten.do?ort=Dresden&hst={}"
    MAX_AGE = timedelta(seconds=30)

    def __init__(self, stops, user_agent="Departure/1.1"):
        self.user_agent = user_agent
        self.cached_data = {}
        self.cached_timestamp = {}
        self.stops = stops

    def _get_cached_data(self, stop_name):
        return self.cached_data.get(stop_name, None)

    def _get_cached_timestamp(self, stop_name):
        return self.cached_timestamp.get(stop_name, None)

    def parse_data(self, s):
        struct = ast.literal_eval(s)
        return [(route, dest, (int(time) if len(time) else 0))
                for route, dest, time
                in struct]

    def get_stop_departure_data(self, stop_name, stop_filter):
        if (self._get_cached_timestamp(stop_name) is not None
                and self._get_cached_data(stop_name) is not None):

            if self.cached_timestamp - datetime.utcnow() <= self.MAX_AGE:
                return self._get_cached_data(stop_name)

        try:
            response, timestamp = hintmodules.utils.http_request(
                self.URL.format(stop_name),
                user_agent=self.user_agent,
                accept="text/html")  # sic: the api returns plaintext, but Content-Type: text/html

            try:
                contents = response.read().decode()
            finally:
                response.close()
        except socket.timeout as err:
            raise
        except urllib.error.HTTPError as err:
            if err.code == 304:
                return self._get_cached_data(stop_name)
            raise

        self.cached_data[stop_name] = stop_filter.filter_departures(
            self.parse_data(contents))
        self.cached_timestamp[stop_name] = timestamp
        return self.cached_data[stop_name]

    def merge_data(self, *data_blocks):
        merged = list(itertools.chain(*data_blocks))
        return merged

    def get_departure_data(self):
        merged = []
        for stop_name, stop_filter in self.stops:
            merged.extend(
                self.get_stop_departure_data(stop_name, stop_filter))
        return merged

    def __call__(self):
        try:
            data = self.get_departure_data()
        except (socket.timeout, urllib.error.URLError, urllib.error.HTTPError) as err:
            warnings.warn(str(err))
            return None
        data.sort(key=lambda x: x[2])
        return data
