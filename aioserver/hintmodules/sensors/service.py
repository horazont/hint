import asyncio
import aioxmpp
import aioxmpp.service

import hintmodules.service
import hintmodules.utils

from . import xso as sensor_xso
from . import backend


class ACLEntry:
    def __init__(self, *, allow_write=False):
        super().__init__()
        self.allow_write = allow_write


class Service(hintmodules.service.HintService):
    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)

        self._acl = {}

    async def configure(self, config):
        backends = config.pop("backend", [])
        sources = config.pop("source", [])

        self._backends = []
        for backend_def in backends:
            class_ = hintmodules.utils.get_class_by_path(
                backend_def["plugin"]
            )
            self._backends.append(
                class_(backend_def)
            )

        self._sources = []
        for i, source_def in enumerate(sources):
            class_ = hintmodules.utils.get_class_by_path(
                source_def["plugin"]
            )
            self._sources.append(
                class_(source_def,
                       self)
            )

        futures = [
            asyncio.ensure_future(source.setup())
            for source in self._sources
        ]

        await asyncio.gather(*futures)

    @aioxmpp.service.iq_handler(
        aioxmpp.IQType.SET,
        sensor_xso.DataPoints)
    async def handle_sensor_submission(self, request):
        if self.ratelimit is not None:
            self.ratelimit.enforce_limit(
                request,
                [
                    ("sensors", "submit"),
                ]
            )

        data = {
            (request.sensor_type, request.sensor_id): [
                (point.timestamp, point.value)
                for point in request.points
            ]
        }

        await self.submit_data(data)

    @aioxmpp.service.iq_handler(
        aioxmpp.IQType.SET,
        sensor_xso.DataPointsEx)
    async def handle_sensor_submission_ex(self, request):
        if self.ratelimit is not None:
            self.ratelimit.enforce_limit(
                request,
                [
                    ("sensors", "submit"),
                ]
            )

        data = {}
        for point in request.points:
            series = data.setdefault((point.sensor_type, point.sensor_id), [])
            series.append((point.timestamp, point.value))

        await self.submit_data(data)

    async def submit_data(self, data):
        self.logger.debug("submitting data %r to backends %r",
                          data,
                          self._backends)

        any_succeeded = False
        for (sensor_type, sensor_id), series in data.items():
            for backend_obj in self._backends:
                try:
                    await backend_obj.submit_sensor_values(
                        sensor_type, sensor_id,
                        series
                    )
                except backend.BackendError as exc:
                    self.logger.error(
                        "failed to submit data for sensor_type=%r, "
                        "sensor_id=%r: %r",
                        sensor_type,
                        sensor_id,
                        str(exc),
                    )
                    continue

                any_succeeded = True

        if not any_succeeded:
            raise RuntimeError(
                "failed to store any of the sensor values"
            )
