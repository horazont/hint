from datetime import datetime, timedelta

from sleekxmpp import Iq, Message
from sleekxmpp.xmlstream import register_stanza_plugin, ElementBase, ET, JID

xmlns = "https://xmlns.zombofant.net/xmpp/sensor"
datefmt = "%Y-%m-%dT%H:%M:%SZ"

class Data(ElementBase):
    namespace = xmlns
    name = "data"
    plugin_attrib = "sensor_data"

class Point(ElementBase):
    namespace = xmlns
    name = "p"
    plugin_attrib = "point"
    interfaces = set(
        ("sensor_type", "sensor_id", "time", "raw_value"))

    def get_raw_value(self):
        return int(self._get_attr("rv"))

    def get_sensor_id(self):
        return self._get_attr("sid")

    def get_sensor_type(self):
        return self._get_attr("st")

    def get_time(self):
        return datetime.strptime(self._get_attr("t"), datefmt)

register_stanza_plugin(Iq, Data)

register_stanza_plugin(Data, Point, iterable=True)
