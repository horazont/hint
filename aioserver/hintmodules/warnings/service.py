import aioxmpp.errors
import aioxmpp.pubsub

import hintmodules.service

from . import xso as warnings_xso


class Service(hintmodules.service.HintService):
    ORDER_AFTER = [
        aioxmpp.PubSubClient
    ]

    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)

        self.pubsub = client.summon(aioxmpp.PubSubClient)

        self.client.stream.register_iq_request_coro(
            "get",
            warnings_xso.LookupName,
            self._search_nodes_by_location_name
        )

        self.client.stream.register_iq_request_coro(
            "get",
            warnings_xso.LookupGeoCoord,
            self._search_nodes_by_geocoord
        )

        self.client.stream.register_iq_request_coro(
            "get",
            warnings_xso.SearchAlerts,
            self._search_alerts_by_geocoord
        )

    async def _search_nodes_by_location_name(self, request):
        if self.ratelimit is not None:
            self.ratelimit.enforce_limit(
                request,
                [
                    ("warnings", "namelookup"),
                ])

    async def _search_nodes_by_geocoord(self, request):
        if self.ratelimit is not None:
            self.ratelimit.enforce_limit(
                request,
                [
                    ("warnings", "geolookup"),
                ])

        lat, lon = request.payload.lat, request.payload.lon

        for plugin in self._plugins.values():
            request.payload.items.extend(
                await plugin.search_nodes_by_geocoord(
                    lat, lon
                )
            )

        return request.payload

    async def _search_alerts_by_geocoord(self, request):
        if self.ratelimit is not None:
            self.ratelimit.enforce_limit(
                request,
                [
                    ("warnings", "geolookup"),
                ])

        lat, lon = request.payload.lat, request.payload.lon

        for plugin in self._plugins.values():
            request.payload.items.extend(
                await plugin.search_alerts_by_geocoord(
                    lat, lon
                )
            )

        return request.payload
