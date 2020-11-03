import asyncio
import dataclasses
import typing

import schema

import aioxmpp

from .. import interface
from . import common, transport


@dataclasses.dataclass
class PubSubConfig:
    service: aioxmpp.JID
    node_pattern: str
    id_pattern: str


class PubSubSink(interface.Sink[PubSubConfig]):
    def __init__(self, *,
                 config: PubSubConfig,
                 **kwargs):
        super().__init__(config=config, **kwargs)
        self._pubsub = self.transport.client.summon(aioxmpp.PubSubClient)
        self._service = config.service
        self._node_pattern = config.node_pattern
        self._id_pattern = config.id_pattern

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "service": common.jid,
            "node_pattern": str,
            schema.Optional("id_pattern", default="current"): str
        })

    @classmethod
    def compile_config(cls, cfg: typing.Mapping) -> PubSubConfig:
        return PubSubConfig(**cfg)

    @classmethod
    def supports_transport(cls, transport_type,
                           cfg: PubSubConfig) -> bool:
        return issubclass(transport_type, transport.Transport)

    @classmethod
    def accepts(self, dataclass: interface.DataClass) -> bool:
        return dataclass == interface.DataClass.SAMPLE_BATCH

    async def _ensure_node(self, node: str):
        # TODO: do actual configuration here
        try:
            await self._pubsub.create(
                self._service,
                node,
            )
        except aioxmpp.errors.XMPPError as exc:
            if exc.condition != aioxmpp.ErrorCondition.CONFLICT:
                raise

    async def submit(self, chunk: interface.DataChunk):
        assert chunk.class_ == interface.DataClass.SAMPLE_BATCH

        for batch in chunk.data:
            fmt_args = {
                "module": batch.bare_path.module,
                "instance": batch.bare_path.instance,
                "part": batch.bare_path.part,
                "bare_path": str(batch.bare_path),
                "isotimestamp": batch.timestamp.isoformat(),
            }
            node = self._node_pattern.format(**fmt_args)
            id_ = self._id_pattern.format(**fmt_args)

            data = common.wrap_batch(batch)

            await self._ensure_node(node)
            self.logger.debug(
                "publishing sample batch %r to pub sub node %r at service %s "
                "with id %r",
                batch, node, self._service, id_,
            )
            await self._pubsub.publish(
                self._service,
                node,
                data,
                id_=id_,
            )
