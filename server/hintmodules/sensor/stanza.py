from datetime import datetime, timedelta

from sleekxmpp import Iq, Message
from sleekxmpp.xmlstream import register_stanza_plugin, ElementBase, ET, JID

xmlns = "https://xmlns.zombofant.net/xmpp/sensor"
datefmt = "%Y-%m-%dT%H:%M:%SZ"

class Data(ElementBase):
    namespace = xmlns
    name = "data"
    plugin_attrib = "sensor_data"

class SensorIdentifyingBase:
    def get_sensor_id(self):
        return self._get_attr("sid")

    def set_sensor_id(self, value):
        self._set_attr("sid", value)

    def get_sensor_type(self):
        return self._get_attr("st")

    def set_sensor_type(self, value):
        self._set_attr("st", value)

class RawPoint(ElementBase, SensorIdentifyingBase):
    namespace = xmlns
    name = "p"
    plugin_attrib = "raw_point"
    interfaces = set(
        ("sensor_type", "sensor_id", "time", "raw_value"))

    def get_raw_value(self):
        return int(self._get_attr("rv"))

    def set_raw_value(self, value):
        self._set_attr("rv", str(value))

    def get_time(self):
        return datetime.strptime(self._get_attr("t"), datefmt)

    def set_time(self, dt):
        self._set_attr("t", dt.strftime(datefmt))

class Point(ElementBase, SensorIdentifyingBase):
    namespace = xmlns
    name = "pp"
    plugin_attrib = "point"
    interfaces = set(
        ("sensor_type", "sensor_id", "time", "value"))

    def get_value(self):
        return float(self._get_attr("v"))

    def set_value(self, value):
        self._set_attr("v", str(value))

    def get_time(self):
        return datetime.strptime(self._get_attr("t"), datefmt)

    def set_time(self, dt):
        self._set_attr("t", dt.strftime(datefmt))

class Request(ElementBase, SensorIdentifyingBase):
    namespace = xmlns
    name = "rq"
    plugin_attrib = "request"
    interfaces = set(
        ("sensor_type", "sensor_id"))

register_stanza_plugin(Iq, Data)

register_stanza_plugin(Data, Point, iterable=True)
register_stanza_plugin(Data, Request, iterable=True)
register_stanza_plugin(Data, RawPoint, iterable=True)
