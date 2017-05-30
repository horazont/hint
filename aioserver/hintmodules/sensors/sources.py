import abc
import asyncio
import logging
import random

from datetime import datetime, timedelta

import hintmodules.weather.common
import hintmodules.utils


class Source(metaclass=abc.ABCMeta):
    def __init__(self, config, sensors_svc, *, logger_base=None):
        super().__init__()
        if logger_base is not None:
            self.logger = logger_base.getChild("rrdtoolbackend")
        else:
            self.logger = logging.getLogger(
                ".".join([__name__, type(self).__qualname__])
            )

        self.sensors_svc = sensors_svc

        self.rate, self.clear = hintmodules.utils.parse_timedelta_ex(
            config["rate"]
        )
        self.sensor_id = config["sensor_id"]

    async def get_series(self):
        return []

    async def execute(self):
        series = await self.get_series()
        self.logger.debug("submitting data %r", series)
        try:
            await self.sensors_svc.submit_data(
                {
                    (self.sensor_type, self.sensor_id): series
                }
            )
        except RuntimeError:
            self.logger.warning("failed to submit data")

    async def background_task(self):
        next_run = datetime.utcnow().replace(**self.clear)
        while True:
            self.logger.debug(
                "next scrape for sensor_type=%r, sensor_id=%r is at %s",
                self.sensor_type,
                self.sensor_id,
                next_run,
            )
            now = datetime.utcnow()
            dt = (next_run - now).total_seconds()
            while dt > 0:
                await asyncio.sleep(min(dt, 60))
                now = datetime.utcnow()
                dt = (next_run - now).total_seconds()

            # use a random offset to avoid all tasks hitting the APIs at once
            await asyncio.sleep(random.random()*5)

            await self.execute()

            next_run = next_run + self.rate

    async def setup(self):
        self._task = asyncio.ensure_future(
            self.background_task()
        )
        self._task.add_done_callback(
            hintmodules.utils.log_failure(self.logger)
        )

    async def shutdown(self):
        if not self._task.done():
            self._task.cancel()
        # we want to swallow exceptions, but wait for it to terminate
        await asyncio.wait([self._task])


class ForecastSource(Source):
    def __init__(self, config, sensors_svc, **kwargs):
        super().__init__(config, sensors_svc, **kwargs)
        self.sensor_type = "hintbot-internal:weather"
        self.lat = round(config["lat"], 6)
        self.lon = round(config["lon"], 6)
        self.attribute = config["attribute"]

        if not hasattr(hintmodules.weather.common.Timepoint, self.attribute):
            raise ValueError(
                "{!r} is not a valid attribute for ForecastSource".format(
                    self.attribute,
                )
            )

        if "offset" in config:
            self.offset = hintmodules.utils.parse_timedelta(config["offset"])
        else:
            self.offset = timedelta()

        self.uri = config["uri"]

    async def get_series(self):
        try:
            plugin = self.sensors_svc.weather_svc.get_plugin_by_uri(self.uri)
        except KeyError:
            self.logger.error(
                "weather service does not know plugin with uri %r",
                self.uri,
            )
            return

        data = await plugin.get_data(self.lat, self.lon)

        datapoints, _ = data

        target = datetime.utcnow() + self.offset
        self.logger.debug("target is %s", target)
        datapoints.sort(
            key=lambda x: abs((x.timestamp - target).total_seconds())
        )

        closest = datapoints[0]

        return [
            (
                closest.timestamp,
                getattr(closest, self.attribute),
            )
        ]
