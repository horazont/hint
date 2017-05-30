import asyncio
import json
import logging

import aiohttp

import aioxmpp.node
import aioxmpp.security_layer
import aioxmpp.structs

import hintmodules.ratelimit
import hintmodules.sensors
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
        data = None

    return data


class HintBot:
    def __init__(self, args, config):
        super().__init__()

        jid = aioxmpp.structs.JID.fromstr(
            config["xmpp"]["jid"]
        )

        self._client = aioxmpp.node.PresenceManagedClient(
            jid,
            aioxmpp.make_security_layer(
                config["xmpp"]["password"],
                pin_store=get_pkfprint_store_verifier_factory(
                    config["xmpp"].get("tls_pk_pinstore"),
                ),
                pin_type=aioxmpp.security_layer.PinType.PUBLIC_KEY,
            ),
            logger=logging.getLogger(__name__ + ".client")
        )

        self._ratelimiting = hintmodules.ratelimit.Service(
            config.get("rate_limit", {}),
            logging.getLogger("ratelimit")
        )

        self._warnings_svc = self.summon(
            hintmodules.warnings.Service,
        )

        self._weather_svc = self.summon(
            hintmodules.weather.Service,
        )

        self._sensors_svc = self.summon(
            hintmodules.sensors.Service,
        )
        self._sensors_svc.weather_svc = self._weather_svc

        self._http_headers = [
            ("User-Agent",
             config.get("http-client", {}).get("user_agent", "aiohintbot/1.0"))
        ]

        self._config = config

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
        await self._warnings_svc.configure(
            self._config.get("warnings", {})
        )

        await self._weather_svc.configure(
            self._config.get("weather", {})
        )

        await self._sensors_svc.configure(
            self._config.get("sensors", {})
        )

        try:
            async with self._client.connected():
                while True:
                    await asyncio.sleep(1)
        finally:
            await self._ratelimiting.close()
