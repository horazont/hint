import aioxmpp.pubsub.xso
import aioxmpp.xso as xso

from aioxmpp.utils import namespaces

namespaces.hint_cap = "urn:oasis:names:tc:emergency:cap:1.2"


class AbstractKeyValuePair(xso.XSO):
    name = xso.ChildText(
        (namespaces.hint_cap, "valueName"),
    )

    value = xso.ChildText(
        (namespaces.hint_cap, "value"),
    )


class Parameter(AbstractKeyValuePair):
    TAG = (namespaces.hint_cap, "parameter")


class EventCode(AbstractKeyValuePair):
    TAG = (namespaces.hint_cap, "eventCode")


class GeoCode(AbstractKeyValuePair):
    TAG = (namespaces.hint_cap, "geocode")


class KeyValueType(xso.AbstractType):
    def __init__(self, class_):
        super().__init__()
        self._class = class_

    def get_formatted_type(self):
        return self._class

    def parse(self, obj):
        return (obj.name, obj.value)

    def format(self, pair):
        name, value = pair
        result = self._class()
        result.name = name
        result.value = value
        return result


class PolygonType(xso.AbstractType):
    @staticmethod
    def parse(s):
        coords = s.split()
        pairs = [
            tuple(float(v) for v in coord.split(","))
            for coord in coords
        ]
        return pairs

    @staticmethod
    def format(pairs):
        return " ".join(
            ",".join([str(a), str(b)])
            for a, b in pairs
        )


class Area(xso.XSO):
    TAG = (namespaces.hint_cap, "area")

    description = xso.ChildText(
        (namespaces.hint_cap, "areaDesc")
    )

    altitude = xso.ChildText(
        (namespaces.hint_cap, "altitude"),
        type_=xso.Float(),
        default=None,
    )

    ceiling = xso.ChildText(
        (namespaces.hint_cap, "ceiling"),
        type_=xso.Float(),
        default=None,
    )

    polygon = xso.ChildText(
        (namespaces.hint_cap, "polygon"),
        type_=PolygonType(),
        default=None,
    )

    geocodes = xso.ChildValueMultiMap(
        KeyValueType(GeoCode),
    )


class Info(xso.XSO):
    TAG = (namespaces.hint_cap, "info")

    language = xso.ChildText(
        (namespaces.hint_cap, "language"),
        default=None,
    )

    category = xso.ChildText(
        (namespaces.hint_cap, "category"),
        default=None,
    )

    event = xso.ChildText(
        (namespaces.hint_cap, "event"),
        default=None,
    )

    response_type = xso.ChildText(
        (namespaces.hint_cap, "responseType"),
        default=None
    )

    urgency = xso.ChildText(
        (namespaces.hint_cap, "urgency"),
        default=None,
    )

    severity = xso.ChildText(
        (namespaces.hint_cap, "severity"),
        default=None,
    )

    certainty = xso.ChildText(
        (namespaces.hint_cap, "certainty"),
        default=None,
    )

    event_codes = xso.ChildValueMultiMap(
        KeyValueType(EventCode)
    )

    effective = xso.ChildText(
        (namespaces.hint_cap, "effective"),
        type_=xso.DateTime(),
        default=None,
    )

    onset = xso.ChildText(
        (namespaces.hint_cap, "onset"),
        type_=xso.DateTime(),
        default=None,
    )

    expires = xso.ChildText(
        (namespaces.hint_cap, "expires"),
        type_=xso.DateTime(),
        default=None,
    )

    sender_name = xso.ChildText(
        (namespaces.hint_cap, "senderName"),
        default=None,
    )

    headline = xso.ChildText(
        (namespaces.hint_cap, "headline"),
        default=None,
    )

    description = xso.ChildText(
        (namespaces.hint_cap, "description"),
        default=None,
    )

    instruction = xso.ChildText(
        (namespaces.hint_cap, "instruction"),
        default=None,
    )

    parameters = xso.ChildValueMultiMap(
        KeyValueType(Parameter)
    )

    areas = xso.ChildList(
        [Area],
    )

    def get_area_with_polygon(self):
        for area in self.areas:
            if area.polygon is not None:
                return area
        return None


class Alert(xso.XSO):
    TAG = (namespaces.hint_cap, "alert")

    identifier = xso.ChildText(
        (namespaces.hint_cap, "identifier"),
    )

    sender = xso.ChildText(
        (namespaces.hint_cap, "sender"),
    )

    sent = xso.ChildText(
        (namespaces.hint_cap, "sent"),
        type_=xso.DateTime()
    )

    status = xso.ChildText(
        (namespaces.hint_cap, "status"),
    )

    message_type = xso.ChildText(
        (namespaces.hint_cap, "msgType")
    )

    source = xso.ChildText(
        (namespaces.hint_cap, "source")
    )

    scope = xso.ChildText(
        (namespaces.hint_cap, "scope")
    )

    infos = xso.ChildList([Info])
