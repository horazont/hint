import hintmodules.service

from . import xso as departure_xso


class Service(hintmodules.service.HintService):
    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)

        self.client.stream.register_iq_request_coro(
            "get",
            departure_xso.Query,
            self._query_departures
        )
