import ast
import abc
from datetime import datetime, timedelta
import http.client
import logging
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

    def _extrapolate(self, cache_entry):
        if not hasattr(cache_entry, "original_data"):
            cache_entry.extrapolate_base = \
                cache_entry.expires - self._cache_timeout
            cache_entry.original_data = cache_entry.data[0]

        delay = (datetime.utcnow() - cache_entry.extrapolate_base).total_seconds()

        departures = [
            (route, dest, time - delay)
            for route, dest, time in cache_entry.original_data
        ]
        cache_entry.data = (departures, delay)


    def _not_available(self, err, cache_entry=None):
        if cache_entry:
            self._extrapolate(cache_entry)
        return hintmodules.caching_requester.RequestError(
            str(err),
            back_off=True,
            cache_enrty=cache_entry,
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
                accept="text/html")
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

        departures = self._parse_data(contents)
        cache_entry.data = (departures, 0)
        cache_entry.expires = datetime.utcnow() + self._cache_timeout

        return cache_entry


class Departure(object):
    MAX_AGE = timedelta(seconds=30)

    NAME = "Dresdner Verkehrsbetriebe"

    def __init__(self, stops, user_agent="Departure/1.1"):
        self.stops = stops
        self.requester = DVBRequester(user_agent)

    def get_stop_departure_data(self, stop_name, stop_filter):
        try:
            return self.requester.request(stop_name=stop_name)
        except hintmodules.caching_requester.RequestError as err:
            raise hintmodules.errors.ServiceNotAvailable(self.NAME)

    @staticmethod
    def annotate_age(age, row):
        return row + (age, )

    def get_departure_data(self):
        merged = []
        for stop_name, stop_filter in self.stops:
            rows, age = self.get_stop_departure_data(stop_name, stop_filter)
            merged.extend(
                self.annotate_age(age, row)
                for row in rows)
        return merged

    def __call__(self):
        data = self.get_departure_data()
        data.sort(key=lambda x: x[2])
        return data
