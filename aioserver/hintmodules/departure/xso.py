import aioxmpp.stanza
import aioxmpp.xso as xso

from aioxmpp.utils import namespaces

namespaces.hint_departure = "https://xmlns.zombofant.net/xmpp/hint/departure/1.0"


class DepartureTime(xso.XSO):
    TAG = (namespaces.hint_departure, "dt")

    lane = xso.Attr(
        "lane",
    )

    destination = xso.Attr(
        "destination",
    )

    timestamp = xso.Attr(
        "t",
        type_=xso.DateTime(),
    )

    eta = xso.Attr(
        "eta",
        type_=xso.Float(),
    )


class Stop(xso.XSO):
    TAG = (namespaces.hint_departure, "stop")

    name = xso.Attr(
        "name",
    )

    departures = xso.ChildList([
        DepartureTime,
    ])


@aioxmpp.stanza.IQ.as_payload_class
class Query(xso.XSO):
    TAG = (namespaces.hint_departure, "query")

    stops = xso.ChildList([
        Stop,
    ])
