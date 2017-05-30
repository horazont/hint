import aioxmpp.stanza
import aioxmpp.xso as xso

from aioxmpp.utils import namespaces

namespaces.hint_sensors = "https://xmlns.zombofant.net/xmpp/hint/sensor/1.0"


class Point(xso.XSO):
    TAG = (namespaces.hint_sensors, "p")

    time = xso.Attr(
        "t",
        type_=xso.DateTime(),
    )

    value = xso.Attr(
        "v",
        type_=xso.Float(),
    )


class PointEx(xso.XSO):
    TAG = (namespaces.hint_sensors, "px")

    sensor_id = xso.Attr(
        "sid",
    )

    sensor_type = xso.Attr(
        "st",
    )

    time = xso.Attr(
        "t",
        type_=xso.DateTime(),
    )

    value = xso.Attr(
        "v",
        type_=xso.Float(),
    )


@aioxmpp.stanza.IQ.as_payload_class
class DataPoints(xso.XSO):
    TAG = (namespaces.hint_sensors, "data")

    sensor_id = xso.Attr(
        "sid",
    )

    sensor_type = xso.Attr(
        "st",
    )

    data_points = xso.ChildList([
        Point,
    ])


@aioxmpp.stanza.IQ.as_payload_class
class DataPointsEx(xso.XSO):
    TAG = (namespaces.hint_sensors, "datax")

    data_points = xso.ChildList([
        PointEx,
    ])


@aioxmpp.stanza.IQ.as_payload_class
class Request(xso.XSO):
    TAG = (namespaces.hint_sensors, "request")

    sensor_id = xso.Attr(
        "sid",
    )

    sensor_type = xso.Attr(
        "st",
    )

    time_start = xso.Attr(
        "ts",
        type_=xso.DateTime(),
    )

    time_end = xso.Attr(
        "te",
        type_=xso.DateTime(),
    )
