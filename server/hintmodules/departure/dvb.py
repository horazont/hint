import ast
import abc
import calendar
from datetime import datetime, timedelta
import http.client
import logging
import math
import socket
import warnings
import urllib.error

import hintmodules.utils
import hintmodules.errors
import hintmodules.caching_requester

logger = logging.getLogger()

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

class DVBRequester(hintmodules.caching_requester.AdvancedRequester):
    URL = "http://widgets.vvo-online.de/abfahrtsmonitor/Abfahrten.do?ort=Dresden&hst={}"

    def __init__(self, user_agent):
        super().__init__(
            # this back off is less than the max age of cached data
            # that is fine though; it only implies that back off will
            # take place in the background for the first round
            initial_back_off_time=timedelta(seconds=20),
            back_off_cap=timedelta(minutes=5))
        self._cache_timeout = timedelta(seconds=15)
        self._user_agent = user_agent

    def _not_available(self, err, cache_entry=None):
        return hintmodules.caching_requester.RequestError(
            str(err),
            back_off=True,
            cache_entry=cache_entry,
            use_context=False)

    def _parse_data(self, contents):
        struct = ast.literal_eval(contents)
        return [(route, dest, (int(time) if len(time) else 0))
                for route, dest, time
                in struct]

    def _perform_request(self, expired_cache_entry, stop_name):
        cache_entry = expired_cache_entry
        try:
            response, timestamp = hintmodules.utils.http_request(
                self.URL.format(stop_name),
                user_agent=self._user_agent,
                # sic: the api returns plaintext (json), but Content-Type:
                # text/html
                accept="text/html",
                timeout=5)
            try:
                contents = response.read().decode()
            finally:
                response.close()

        except socket.timeout as err:
            logger.warn("temporarily not available: %s: %s", type(err), err)
            raise self._not_available(err, cache_entry) from err
        except http.client.BadStatusLine as err:
            logger.warn("temporarily not available: %s: %s", type(err), err)
            raise self._not_available(err, cache_entry) from err
        except urllib.error.HTTPError as err:
            if err.code == 304:
                return cache_entry
            raise self._not_available(err, cache_entry) from err
        except urllib.error.URLError as err:
            logger.warn("temporarily not available: %s: %s", type(err), err)
            raise self._not_available(err, cache_entry) from err

        if cache_entry is None:
            cache_entry = hintmodules.caching_requester.CacheEntry()

        departures = self._parse_data(contents)
        cache_entry.data = (departures, datetime.utcnow())
        cache_entry.expires = cache_entry.data[1] + self._cache_timeout

        return cache_entry

    def _get_backing_off_result(self, expired_cache_entry, stop_name):
        cache_entry = expired_cache_entry
        return cache_entry

class Departure(object):
    MAX_AGE = timedelta(seconds=30)

    NAME = "Dresdner Verkehrsbetriebe"

    def __init__(self, stops, user_agent="Departure/1.1"):
        self.stops = stops
        self.requester = DVBRequester(user_agent)

    def get_stop_departure_data(self, stop_name, stop_filter):
        try:
            data, timestamp = self.requester.request(stop_name=stop_name)
        except (hintmodules.caching_requester.RequestError,
                hintmodules.caching_requester.BackingOff) as err:
            raise hintmodules.errors.ServiceNotAvailable(
                "{} did not respond properly ({})".format(
                    self.NAME,
                    err))

        data = stop_filter.filter_departures(data)
        return data, timestamp

    @staticmethod
    def annotate_timestamp(unix_timestamp, row):
        return row + (unix_timestamp, )

    def get_departure_data(self):
        merged = []
        for stop_name, stop_filter in self.stops:
            rows, timestamp = self.get_stop_departure_data(stop_name,
                                                           stop_filter)
            unix_timestamp = calendar.timegm(timestamp.utctimetuple())
            merged.extend(
                self.annotate_timestamp(unix_timestamp, row)
                for row in rows)
        return merged

    def __call__(self):
        data = self.get_departure_data()
        data.sort(key=lambda x: x[2])
        return data
