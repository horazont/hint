import asyncio
import functools
import logging
import numbers
import random
import typing

from datetime import datetime

import schema

import hintlib.sample
import hintlib.services

import metric_relay.interface


class NullTransport(metric_relay.interface.Transport):
    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({})


class LogTransport(metric_relay.interface.Transport):
    def __init__(self, *, config, **kwargs):
        super().__init__(config=config, **kwargs)
        self.output = logging.getLogger(config)

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "name": str,
        })

    @classmethod
    def compile_config(cls, cfg: typing.Mapping) -> str:
        return cfg["name"]


class LogSink(metric_relay.interface.Sink):
    @classmethod
    def supports_transport(
            cls,
            transport_class: type,
            config: object) -> bool:
        return issubclass(transport_class, LogTransport)

    @classmethod
    def accepts(self, dataclass: metric_relay.interface.DataClass) -> bool:
        return True

    async def submit(self, data: metric_relay.interface.DataChunk):
        self.logger.debug("received sample: %r", data)
        self.transport.output.debug("%r", data)


class RandomSampleSource(metric_relay.interface.Source):
    def __init__(self, *, config, **kwargs):
        super().__init__(config=config, **kwargs)
        self._parts = config["parts"]

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "parts": [{
                "module": str,
                "part": str,
                schema.Optional("instance"): str,
                "interval": numbers.Real,
                "subparts": [{
                    schema.Optional("name", default=None): str,
                    "min": numbers.Real,
                    "max": numbers.Real,
                }],
            }]
        })

    @classmethod
    def supports_transport(
            cls,
            transport_class: type,
            config: object) -> bool:
        return issubclass(transport_class, NullTransport)

    @classmethod
    def emits(self, dataclass: metric_relay.interface.DataClass) -> bool:
        return dataclass == metric_relay.interface.DataClass.SAMPLE_BATCH

    async def _generate(self, part):
        path = hintlib.sample.SensorPath(
            module=part["module"],
            part=part["part"],
            instance=part["instance"],
        )
        bare_path = path.replace(instance=None)
        while True:
            timestamp = datetime.utcnow()
            samples = {}
            for subpart in part["subparts"]:
                range_ = subpart["max"] - subpart["min"]
                value = random.random() * range_ + subpart["min"]
                samples[subpart["name"]] = value

            sample = hintlib.sample.SampleBatch(
                timestamp=timestamp,
                bare_path=bare_path,
                samples=samples,
            )
            self.logger.debug("generated sample: %r", sample)
            await self._emit(metric_relay.interface.DataChunk.from_sample_batch(
                sample
            ))
            await asyncio.sleep(part["interval"])

    async def run(self):
        tasks = []
        for part in self._parts:
            tasks.append(hintlib.services.RestartingTask(
                functools.partial(self._generate, part),
                logger=self.logger,
            ))

        try:
            for task in tasks:
                task.start()

            while True:
                await asyncio.sleep(1)
        finally:
            for task in tasks:
                task.stop()
            await asyncio.gather(*(
                task.wait_for_termination()
                for task in tasks
            ), return_exceptions=True)
