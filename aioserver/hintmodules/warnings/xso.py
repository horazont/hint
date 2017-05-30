import aioxmpp.disco.xso
import aioxmpp.pubsub.xso
import aioxmpp.stanza
import aioxmpp.xso as xso

from aioxmpp.utils import namespaces

from .cap import Alert

namespaces.hint_warnings = "https://xmlns.zombofant.net/xmpp/hint/warnings/1.0"


@aioxmpp.stanza.IQ.as_payload_class
class SearchAlerts(xso.XSO):
    TAG = (namespaces.hint_warnings, "alerts")

    lat = xso.Attr(
        "lat",
        type_=xso.Float(),
    )

    lon = xso.Attr(
        "lon",
        type_=xso.Float(),
    )

    items = xso.ChildList([
        Alert
    ])


@aioxmpp.stanza.IQ.as_payload_class
class LookupName(xso.XSO):
    TAG = (namespaces.hint_warnings, "lookup-name")

    name = xso.Attr(
        "name",
    )

    items = xso.ChildList([
        aioxmpp.disco.xso.Item
    ])


@aioxmpp.stanza.IQ.as_payload_class
class LookupGeoCoord(xso.XSO):
    TAG = (namespaces.hint_warnings, "lookup-geocoord")

    lat = xso.Attr(
        "lat",
        type_=xso.Float(),
    )

    lon = xso.Attr(
        "lon",
        type_=xso.Float(),
    )

    items = xso.ChildList([
        aioxmpp.disco.xso.Item
    ])


aioxmpp.pubsub.xso.as_payload_class(Alert)
