import asyncio
import dataclasses
import typing

from datetime import timedelta

import schema

import aioxmpp.service
from aioxmpp.utils import namespaces

import hintlib.core
import hintlib.services
import hintlib.sample
import hintlib.xso

from .. import interface
from . import common


class SubmissionEndpoint(aioxmpp.service.Service):
    ORDER_AFTER = [hintlib.services.Buddies]

    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)
        self.__buddies = self.dependencies[hintlib.services.Buddies]
        self.emit = None

    async def _emit_sample_batches(self, batches_xso):
        batches = []
        for batch_xso in batches_xso.batches:
            bare_path = hintlib.sample.SensorPath(
                module=batch_xso.module,
                part=hintlib.sample.Part(batch_xso.part),
                instance=batch_xso.instance,
            )
            batch = hintlib.sample.SampleBatch(
                timestamp=batch_xso.timestamp,
                bare_path=bare_path,
                samples={
                    sample_xso.subpart: sample_xso.value
                    for sample_xso in batch_xso.samples
                }
            )
            batches.append(batch)

        await self.emit(interface.DataChunk.from_sample_batches(batches))

    async def _emit_stream(self, stream_xso):
        await self.emit(interface.DataChunk.from_stream_block(
            hintlib.sample.StreamBlock(
                timestamp=stream_xso.t0,
                path=hintlib.sample.SensorPath(
                    module=stream_xso.module,
                    part=stream_xso.part,
                    instance=stream_xso.instance,
                    subpart=stream_xso.subpart,
                ),
                seq0=stream_xso.seq0,
                period=timedelta(microseconds=stream_xso.period),
                data=hintlib.sample.EncodedStreamData(
                    sample_type=stream_xso.sample_type,
                    data=stream_xso.data,
                    compressed=True,
                ),
            )
        ))

    @aioxmpp.service.iq_handler(aioxmpp.IQType.SET, hintlib.xso.Query)
    async def sensor_query(self, iq):
        allowed_jids = self.__buddies.get_by_permissions({"submission"})

        if iq.from_.bare() not in allowed_jids:
            raise aioxmpp.XMPPAuthError(
                (namespaces.stanzas, "forbidden"),
            )

        if iq.payload.stream is not None:
            return (await self._emit_stream(iq.payload.stream))
        elif iq.payload.sample_batches is not None:
            return (await self._emit_sample_batches(
                iq.payload.sample_batches
            ))
        else:
            raise NotImplementedError


@dataclasses.dataclass
class BuddySourceConfig:
    required_permissions: typing.Set[str]


class BuddySource(Source[BuddySourceConfig]):
    def __init__(self, logger, client, **kwargs):
        super().__init__(logger, **kwargs)
        self._client = client
        self._endpoint = self._client.summon(SubmissionEndpoint)
        self._endpoint.emit = self._emit

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "required_permissions": common.set_type(str),
        })

    @classmethod
    def emits(self, dataclass: interface.DataClass) -> bool:
        return True

    async def run(self):
        async with self._client.connected() as stream:
            await super().run()
