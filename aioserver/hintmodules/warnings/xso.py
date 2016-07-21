import aioxmpp.disco.xso
import aioxmpp.pubsub.xso
import aioxmpp.stanza
import aioxmpp.xso as xso

from aioxmpp.utils import namespaces

namespaces.hint_warnings = "https://xmlns.zombofant.net/xmpp/hint/warnings/1.0"


class Polygon(xso.XSO):
    TAG = (namespaces.hint_warnings, "polygon")

    shape = xso.Text()


class HintWarning(xso.XSO):
    TAG = (namespaces.hint_warnings, "warning")

    is_preliminary = xso.Attr(
        "preliminary",
        type_=xso.Bool(),
        default=False,
    )

    event = xso.Attr(
        "event",
    )

    description = xso.ChildText(
        (namespaces.hint_warnings, "description"),
    )

    headline = xso.ChildText(
        (namespaces.hint_warnings, "headline"),
    )

    instruction = xso.ChildText(
        (namespaces.hint_warnings, "instruction"),
    )

    area = xso.ChildList([
        Polygon
    ])

    exclude_area = xso.ChildList([
        Polygon
    ])

    level = xso.Attr(
        "level",
        type_=xso.Integer(),
        default=None,
    )

    type_ = xso.Attr(
        "type",
        type_=xso.Integer(),
        default=None,
    )

    start = xso.Attr(
        "start",
        type_=xso.DateTime(),
    )

    end = xso.Attr(
        "end",
        type_=xso.DateTime(),
        default=None,
    )

    altitude_start = xso.Attr(
        "altitude-start",
        type_=xso.Float(),
        default=None,
    )

    altitude_end = xso.Attr(
        "altitude-end",
        type_=xso.Float(),
        default=None,
    )


@aioxmpp.stanza.IQ.as_payload_class
class HintWarnings(xso.XSO):
    TAG = (namespaces.hint_warnings, "warnings")

    lat = xso.Attr(
        "lat",
        type_=xso.Float(),
    )

    lon = xso.Attr(
        "lon",
        type_=xso.Float(),
    )

    items = xso.ChildList([
        aioxmpp.disco.xso.HintWarning
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


aioxmpp.pubsub.xso.Item.register_child(
    aioxmpp.pubsub.xso.Item.registered_payload,
    HintWarning,
)
