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
        self._configured_nodes = set()
        self.transport.client.on_stream_destroyed.connect(self._drop_state)

    def _drop_state(self):
        self._configured_nodes.clear()

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
        if node in self._configured_nodes:
            return

        # TODO: do actual configuration here
        self.logger.debug("creating node %r", node)
        try:
            await self._pubsub.create(
                self._service,
                node,
            )
        except aioxmpp.errors.XMPPError as exc:
            if exc.condition != aioxmpp.ErrorCondition.CONFLICT:
                raise
            self.logger.debug("node %r exists already, moving on", node)
        else:
            self.logger.info("created pubsub node %r at service %s",
                             node, self._service)

        self._configured_nodes.add(node)

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
            try:
                await self._pubsub.publish(
                    self._service,
                    node,
                    data,
                    id_=id_,
                )
            except aioxmpp.errors.XMPPError as exc:
                self.logger.warning(
                    "failed to publish to node %r at service %s (%s); trying "
                    "reconfiguration",
                    node, self._service, exc,
                )
                self._configured_nodes.discard(node)
                try:
                    await self._ensure_node(node)
                except aioxmpp.errors.XMPPError as exc:
                    self.logger.warning(
                        "failed to (re-)create node %r at service %s after "
                        "publish error; giving up on publish",
                        node, self._service,
                    )
                    raise

                try:
                    await self._pubsub.publish(
                        self._service,
                        node,
                        data,
                        id_=id_,
                    )
                except aioxmpp.errors.XMPPError as exc:
                    self.logger.error(
                        "failed to publish to node %r at service %s (%s); "
                        "ensuring presence of the node did not help. giving "
                        "up and re-raising last error",
                    )
                    raise
