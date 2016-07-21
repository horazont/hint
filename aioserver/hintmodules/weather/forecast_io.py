import asyncio
import contextlib
import email.utils as eutils
import math
import json

from datetime import datetime, timedelta

import aiohttp
import aiohttp.errors

import hintmodules.cache

from . import common


def parse_http_date(httpdate):
    return datetime(*eutils.parsedate(httpdate)[:6])


class Requester(hintmodules.cache.AdvancedRequester):
    URL = "https://api.forecast.io/forecast/{apikey}/{lat},{lon}"

    def __init__(self, logger,
                 http_session_factory,
                 apikey,
                 mock_data=None,
                 dump=None):
        super().__init__()
        self.logger = logger
        self.http_session_factory = http_session_factory
        self.max_cache_over_expiry = timedelta(minutes=45)
        self.apikey = apikey
        self.mock_data = mock_data
        self.dump = dump

    def _is_too_stale(self, cache_entry):
        age = datetime.utcnow() - cache_entry.expires
        return age > self.max_cache_over_expiry

    def _get_backing_off_result(self, expired_cache_entry=None, **kwargs):
        # if the cache entry is not 'too old', return it, otherwise make it
        # explicit that we’re currently backing off.

        if expired_cache_entry is not None:
            if self._is_too_stale(expired_cache_entry):
                return None

        return expired_cache_entry

    def _decode_response_into(self, data, cache_entry):
        datapoints = []
        intervals = []

        for hourly_data in data["hourly"]["data"]:
            timestamp = datetime.utcfromtimestamp(hourly_data["time"])
            start = timestamp
            end = timestamp + timedelta(hours=1)
            center = timestamp + timedelta(minutes=30)

            datapoint = common.Timepoint(center)
            datapoint.temperature = hourly_data["temperature"] + 273.15
            datapoint.humidity = hourly_data["humidity"]
            datapoint.apparent_temperature = \
                hourly_data["apparentTemperature"] + 273.15
            datapoint.dewpoint_temperature = hourly_data["dewPoint"] + 273.15
            datapoint.ozone = hourly_data["ozone"]
            datapoint.wind_speed = hourly_data["windSpeed"]
            datapoint.cloud_cover = hourly_data["cloudCover"]
            datapoint.wind_bearing = hourly_data["windBearing"] / 180 * math.pi

            interval = common.Interval(start, end)
            interval.precipitation = hourly_data["precipIntensity"]
            interval.precipitation_probability = \
                hourly_data["precipProbability"]

            datapoints.append(datapoint)
            intervals.append(interval)

        cache_entry.data = datapoints, intervals

    async def _perform_request(self, lat, lon,
                               expired_cache_entry=None):
        if self.mock_data is not None:
            self.logger.warning("using mock data")
            cache_entry = expired_cache_entry or \
                hintmodules.cache.CacheEntry()
            self._decode_response_into(
                self.mock_data,
                cache_entry
            )
            return cache_entry

        with contextlib.ExitStack() as stack:
            session = stack.enter_context(
                self.http_session_factory(),
            )
            stack.enter_context(aiohttp.Timeout(5))

            headers = []
            if expired_cache_entry is not None:
                if expired_cache_entry.last_modified is not None:
                    headers.append(
                        ("If-Modified-Since",
                         expired_cache_entry.last_modified)
                    )

            url = self.URL.format(
                lat=lat,
                lon=lon,
                apikey=self.apikey
            )

            response = None
            try:
                async with session.get(url,
                                       params={
                                           "units": "si",
                                       },
                                       headers=headers) as response:
                    self.logger.debug(
                        "response: '%d: %s' (headers=%r)",
                        response.status,
                        response.reason,
                        response.headers,
                    )

                    if response.status == 304:
                        # cache is still valid
                        self.logger.info(
                            "re-using cached data (received 304)"
                        )
                        return expired_cache_entry

                    elif response.status != 200:
                        raise hintmodules.cache.RequestError(
                            response.reason,
                            back_off=False,
                            cache_entry=None
                        )

                    last_modified = response.headers.get("Last-Modified")
                    expires = parse_http_date(response.headers.get("Expires"))
                    data = await response.json()

            except aiohttp.errors.ContentEncodingError:
                if response.status == 304:
                    # some problem with aiohttp?
                    return expired_cache_entry
                else:
                    raise
            except asyncio.TimeoutError:
                cache_entry = expired_cache_entry
                if cache_entry is not None and self._is_too_stale(cache_entry):
                    cache_entry = None

                raise hintmodules.cache.RequestError(
                    "timeout",
                    back_off=True,
                    cache_entry=cache_entry,
                )
            else:
                cache_entry = (expired_cache_entry or
                               hintmodules.cache.CacheEntry())

        self._decode_response_into(data, cache_entry)
        cache_entry.expires = expires
        cache_entry.last_modified = last_modified

        return cache_entry


class Service:
    DESCRIPTION = (
        "The Forecast API allows you to look up the weather anywhere on the globe."
    )
    DEFAULT_LICENSE = "unknown"

    def __init__(self, service, section):
        super().__init__()
        self.service = service
        self.logger = service.logger.getChild("forecastio")

        self._cache = {}

        mock_data = None
        if section.get("mock_data", fallback=None):
            with open(section.get("mock_data"), "r") as f:
                mock_data = json.load(f)

        self.requester = Requester(
            self.logger,
            service.http_session_factory,
            apikey=section.get("apikey"),
            mock_data=mock_data,
            dump=section.get("dump", fallback=None)
        )

    async def get_data(self, lat, lon):
        data = await self.requester.request(
            lat=lat,
            lon=lon,
        )

        return data
