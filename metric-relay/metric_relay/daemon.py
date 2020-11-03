import asyncio
import dataclasses
import logging
import typing

import hintlib.services

from . import interface, queue


@dataclasses.dataclass
class Route:
    from_: interface.Source
    to: interface.Sink
    persistent: bool


def fanout(logger, sinks):
    async def fanout_impl(data: interface.DataChunk):
        try:
            await asyncio.gather(*(
                sink(data)
                for sink in sinks
            ))
        except Exception as exc:
            logger.error("failed to fanout sample to some or all sinks",
                         exc_info=True)
            raise

    return fanout_impl


class MetricRelay:
    def __init__(
            self,
            *,
            transports: typing.List[interface.Transport],
            sources: typing.List[interface.Source],
            sinks: typing.List[interface.Sink],
            routes: typing.List[Route],
            logger: logging.Logger,
            ):
        super().__init__()
        self._transports = transports
        self._sources = sources
        self._sinks = sinks
        self._logger = logger
        self._queues = []

        routes_by_source = {}
        for route in routes:
            routes_by_source.setdefault(route.from_, []).append(route)

        for source, routes in routes_by_source.items():
            queues = [
                queue.EphemeralQueue(
                    logger=route.to.logger.getChild("input-queue"),
                    max_depth=16,
                    sink=route.to.submit,
                    overflow_policy=queue.OverflowPolicy.DROP_OLD,
                )
                for route in routes
            ]
            self._queues.extend(queues)
            sinks = [q.push for q in queues]
            fanout_instance = fanout(logger.getChild("fanout"), sinks)
            source.on_data = fanout_instance

    async def run(self):
        tasks = []
        for transport in self._transports:
            tasks.append(hintlib.services.RestartingTask(
                transport.run,
                logger=transport.logger.getChild("supervisor"),
            ))
        for source in self._sources:
            tasks.append(hintlib.services.RestartingTask(
                source.run,
                logger=source.logger.getChild("supervisor"),
            ))
        for sink in self._sinks:
            tasks.append(hintlib.services.RestartingTask(
                sink.run,
                logger=sink.logger.getChild("supervisor"),
            ))
        for q in self._queues:
            tasks.append(hintlib.services.RestartingTask(
                q.run,
                logger=q.logger.getChild("supervisor"),
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
