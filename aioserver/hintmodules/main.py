import asyncio
import functools
import json
import logging

import aiohttp

import aioxmpp.node
import aioxmpp.security_layer
import aioxmpp.structs

import hintmodules.ratelimit
import hintmodules.warnings
import hintmodules.weather


@asyncio.coroutine
def password_provider(password, jid, nattempt):
    if nattempt > 1:
        return None
    return password


def get_pkfprint_store_verifier_factory(store_path):
    if store_path is not None:
        with open(store_path, "r") as f:
            data = json.load(f)
    else:
        data = {}

    store = aioxmpp.security_layer.PublicKeyPinStore()
    store.import_from_json(data)
    del data

    @asyncio.coroutine
    def fail_always(*args, **kwargs):
        return False

    def verifier_factory():
        return aioxmpp.security_layer.PinningPKIXCertificateVerifier(
            store.query,
            fail_always
        )

    return verifier_factory


class HintBot:
    def __init__(self, args, config):
        super().__init__()

        jid = aioxmpp.structs.JID.fromstr(
            config.get("xmpp", "jid")
        )

        self._client = aioxmpp.node.PresenceManagedClient(
            jid,
            aioxmpp.security_layer.security_layer(
                aioxmpp.security_layer.STARTTLSProvider(
                    aioxmpp.security_layer.default_ssl_context,
                    get_pkfprint_store_verifier_factory(
                        config.get("xmpp", "tls_pk_pinstore", fallback=None),
                    ),
                ),
                [
                    aioxmpp.security_layer.PasswordSASLProvider(
                        functools.partial(
                            password_provider,
                            config.get("xmpp", "password"),
                        )
                    )
                ]
            ),
            logger=logging.getLogger(__name__ + ".client")
        )

        self._ratelimiting = hintmodules.ratelimit.Service(
            config,
            logging.getLogger("ratelimit")
        )

        self._warnings_svc = self.summon(
            hintmodules.warnings.Service
        )

        self._weather_svc = self.summon(
            hintmodules.weather.Service
        )

        self._http_headers = [
            ("User-Agent",
             config.get(
                 "http-client",
                 "user_agent",
                 fallback="aiohintbot/1.0")),
        ]

        for section in config.sections():
            if section.startswith("warnings:"):
                self._warnings_svc.load_plugin(
                    config[section],
                )
            elif section.startswith("weather:"):
                self._weather_svc.load_plugin(
                    config[section],
                )

    def summon(self, hint_service):
        svc = self._client.summon(hint_service)
        svc.ratelimit = self._ratelimiting
        svc.http_session_factory = self.get_session
        return svc

    def get_session(self, *args, **kwargs):
        return aiohttp.ClientSession(
            *args,
            headers=self._http_headers,
            **kwargs,
        )

    async def run(self):
        # FIXME: use PEP 492 as soon as we have it

        async with self._client.connected():
            while True:
                await asyncio.sleep(1)

        await self._ratelimiting.close()
