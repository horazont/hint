import asyncio
import dataclasses
import typing

import schema

import aioxmpp

import hintlib.services

from .. import interface
from . import common


@dataclasses.dataclass
class XMPPConfig:
    address: aioxmpp.JID
    password: str
    host: typing.Optional[str]
    port: int
    public_key_pin: typing.Optional[typing.Mapping[str, typing.List[str]]]
    buddies: typing.Mapping[aioxmpp.JID, typing.Set[str]]


class Transport(interface.Transport[XMPPConfig]):
    def __init__(self, *, config: XMPPConfig, **kwargs):
        super().__init__(config=config, **kwargs)

        security_args = {}
        if config.public_key_pin is not None:
            security_args["pin_store"] = config.public_key_pin
            security_args["pin_type"] = \
                aioxmpp.security_layer.PinType.PUBLIC_KEY

        override_peer = []
        if config.host is not None:
            override_peer.append(
                (
                    config.host, config.port,
                    aioxmpp.connector.STARTTLSConnector(),
                )
            )

        self.client = aioxmpp.Client(
            config.address,
            aioxmpp.make_security_layer(config.password,
                                        **security_args),
            override_peer=override_peer,
            logger=self.logger.getChild("client"),
        )
        self.buddies = self.client.summon(hintlib.services.Buddies)
        self.buddies.set_buddies(config.buddies)

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "address": common.jid,
            "password": str,
            "host": str,
            schema.Optional("port", default=5222): int,
            schema.Optional("public_key_pin", default=None): {
                schema.Optional(str): [str],
            },
            schema.Optional("buddies", default={}): {
                common.jid: common.set_type(str),
            },
        })

    @classmethod
    def compile_config(cls, cfg: typing.Mapping) -> XMPPConfig:
        return XMPPConfig(**cfg)

    async def run(self):
        stop_signal = asyncio.Future()
        self.client.on_stopped.connect(
            stop_signal,
            self.client.on_stopped.AUTO_FUTURE,
        )
        self.client.on_failure.connect(
            stop_signal,
            self.client.on_failure.AUTO_FUTURE,
        )
        async with self.client.connected() as stream:
            await stop_signal
